#include "media/PortableSegmentRecorder.h"

#include "core/FilenameAllocator.h"
#include "platform/MonitorEnumerator.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTemporaryFile>

#include <chrono>
#include <algorithm>
#include <utility>

namespace LastFrame::Media {

PortableSegmentRecorder::PortableSegmentRecorder(QObject* parent) : QObject(parent) {
    segmentTimer_.setInterval(500);
    connect(&segmentTimer_, &QTimer::timeout, this, &PortableSegmentRecorder::reapSegments);
}

PortableSegmentRecorder::~PortableSegmentRecorder() {
    stop();
    for (ExportJob* job : std::as_const(exports_)) {
        if (job->process != nullptr) {
            job->process->kill();
            job->process->deleteLater();
        }
        if (job->listFile != nullptr) {
            job->listFile->close();
            job->listFile->remove();
            job->listFile->deleteLater();
        }
        delete job;
    }
    exports_.clear();
}

void PortableSegmentRecorder::start(const LastFrame::Core::Settings& settings) {
    if (recording_) {
        return;
    }
    settings_ = settings;
    ffmpegPath_ = locateFfmpeg();
    if (ffmpegPath_.isEmpty()) {
        emit error(QStringLiteral("FFmpeg не найден. Поместите ffmpeg.exe рядом с LastFrame.exe или добавьте его в PATH."));
        return;
    }

    segmentDirectory_ = settings_.buffer.temporaryDirectory.empty()
                            ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/segments"
                            : QString::fromStdString(settings_.buffer.temporaryDirectory.string());
    QDir().mkpath(segmentDirectory_);
    const QDir directory(segmentDirectory_);
    for (const QFileInfo& file : directory.entryInfoList({QStringLiteral("segment_*.mkv"),
                                                           QStringLiteral("concat-*.txt"),
                                                           QStringLiteral("*.tmp")},
                                                          QDir::Files)) {
        QFile::remove(file.absoluteFilePath());
    }
    attemptedVideoOnlyFallback_ = false;
    paused_ = false;
    startProcess(true);
}

void PortableSegmentRecorder::pause() {
    if (!recording_ || paused_) {
        return;
    }
    recording_ = false;
    if (captureProcess_ != nullptr) {
        captureProcess_->terminate();
        if (!captureProcess_->waitForFinished(1500)) {
            captureProcess_->kill();
            captureProcess_->waitForFinished(1000);
        }
    }
    paused_ = true;
    segmentTimer_.stop();
    emit pausedChanged(true);
    emit message(QStringLiteral("Буфер приостановлен; накопленные сегменты сохранены."));
}

void PortableSegmentRecorder::resume() {
    if (!paused_) {
        return;
    }
    paused_ = false;
    attemptedVideoOnlyFallback_ = false;
    startProcess(processHasAudio_);
    emit pausedChanged(false);
}

void PortableSegmentRecorder::stop() {
    const bool wasActive = recording_ || paused_;
    recording_ = false;
    if (captureProcess_ != nullptr) {
        // waitForFinished() pumps the event loop; disconnect first so the
        // finished callback cannot clear captureProcess_ mid-cleanup.
        QProcess* process = captureProcess_;
        process->disconnect(this);
        process->terminate();
        if (!process->waitForFinished(1500)) {
            process->kill();
            process->waitForFinished(1000);
        }
        process->deleteLater();
        captureProcess_ = nullptr;
    }
    paused_ = false;
    segmentTimer_.stop();
    if (wasActive) {
        emit stopped();
    }
}

void PortableSegmentRecorder::clearBuffer() {
    if (segmentDirectory_.isEmpty()) {
        return;
    }
    const QDir directory(segmentDirectory_);
    for (const QFileInfo& file : directory.entryInfoList({QStringLiteral("segment_*.mkv")}, QDir::Files)) {
        QFile::remove(file.absoluteFilePath());
    }
    emit message(QStringLiteral("Буфер очищен; сохранённые клипы не затронуты."));
}

void PortableSegmentRecorder::saveClip() {
    using namespace std::chrono;
    if (rateLimiter_.tryAccept(steady_clock::now()) == Core::RateLimitResult::Cooldown) {
        emit error(QStringLiteral("Слишком много сохранений. Новые снимки временно заблокированы на 5 секунд."));
        return;
    }

    QStringList files = segmentFiles();
    // The segment muxer keeps the newest file open while FFmpeg is running.
    // Never hand that incomplete Matroska file to the concat demuxer.
    if (recording_ && files.size() > 1) {
        files.removeLast();
    }
    if (files.isEmpty()) {
        emit error(QStringLiteral("В буфере пока нет готовых сегментов."));
        return;
    }
    if (exports_.size() >= settings_.storage.maxQueueLength) {
        emit error(QStringLiteral("Очередь экспорта заполнена. Дождитесь завершения предыдущих клипов."));
        return;
    }

    const QString clipsDirectory = settings_.storage.clipsDirectory.empty()
                                       ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/LastFrame"
                                       : QString::fromStdString(settings_.storage.clipsDirectory.string());
    QDir().mkpath(clipsDirectory);
    QString container = QString::fromStdString(settings_.video.container).toLower();
    if (container != QStringLiteral("mp4") && container != QStringLiteral("mkv") &&
        container != QStringLiteral("webm")) {
        container = QStringLiteral("mp4");
    }
    const auto target = Core::FilenameAllocator::allocate(
        std::filesystem::path(clipsDirectory.toStdString()), system_clock::now(), container.toStdString());
    const QString finalPath = QString::fromStdString(target.string());
    QFile reservation(finalPath);
    if (!reservation.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
        emit error(QStringLiteral("Не удалось зарезервировать имя итогового клипа."));
        return;
    }
    reservation.close();

    auto* listFile = new QTemporaryFile(segmentDirectory_ + "/concat-XXXXXX.txt", this);
    listFile->setAutoRemove(false);
    if (!listFile->open()) {
        QFile::remove(finalPath);
        listFile->deleteLater();
        emit error(QStringLiteral("Не удалось создать список сегментов для экспорта."));
        return;
    }
    QByteArray content("ffconcat version 1.0\n");
    for (const QString& file : files) {
        content += "file '" + escapeConcatPath(file).toUtf8() + "'\n";
        // Every closed capture segment is cut on a one-second boundary. The
        // explicit duration gives the concat demuxer a monotonic timeline
        // when Matroska packet timestamps restart at zero per segment.
        content += "duration 1.0\n";
    }
    listFile->write(content);
    listFile->close();

    auto* job = new ExportJob;
    job->listFile = listFile;
    job->listPath = listFile->fileName();
    job->temporaryPath = QString::fromStdString(target.string());
    job->temporaryPath += ".tmp";
    job->finalPath = finalPath;
    QFile::remove(job->temporaryPath);
    job->process = new QProcess(this);
    job->process->setProcessChannelMode(QProcess::MergedChannels);
    exports_.push_back(job);

    connect(job->process, &QProcess::finished, this,
            [this, job](int code, QProcess::ExitStatus status) { finishExport(job, code, status); });
    connect(job->process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit error(QStringLiteral("FFmpeg не смог запустить экспорт клипа."));
    });

    QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-fflags"), QStringLiteral("+genpts"),
        QStringLiteral("-f"), QStringLiteral("concat"), QStringLiteral("-safe"), QStringLiteral("0"),
        QStringLiteral("-i"), job->listPath,
    };
    if (container == QStringLiteral("mkv")) {
        // FFmpeg's Matroska muxer rejects the restarted packet timestamps
        // from concat'ed one-second segments when they are stream-copied.
        // Re-encode this less latency-sensitive format for a valid timeline.
        args << QStringLiteral("-c:v") << QStringLiteral("libx264")
             << QStringLiteral("-preset") << QStringLiteral("veryfast")
             << QStringLiteral("-crf") << QStringLiteral("18")
             << QStringLiteral("-c:a") << QStringLiteral("aac");
    } else {
        args << QStringLiteral("-c") << QStringLiteral("copy");
    }
    if (container == QStringLiteral("mp4")) {
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }
    args << QStringLiteral("-f")
         << (container == QStringLiteral("mkv") ? QStringLiteral("matroska") : container)
         << job->temporaryPath;
    job->process->start(ffmpegPath_, args);
    emit message(QStringLiteral("Экспорт клипа поставлен в очередь."));
}

void PortableSegmentRecorder::reapSegments() {
    const QStringList files = segmentFiles();
    const int keepCount = std::max(3, settings_.buffer.durationSeconds + 2);
    QDir directory(segmentDirectory_);
    for (int index = 0; index < files.size() - keepCount; ++index) {
        QFile::remove(files.at(index));
    }
}

void PortableSegmentRecorder::processFinished(const int exitCode, const QProcess::ExitStatus status) {
    Q_UNUSED(status)
    if (recording_ && exitCode != 0 && processHasAudio_ && !attemptedVideoOnlyFallback_) {
        attemptedVideoOnlyFallback_ = true;
        emit message(QStringLiteral("Системный звук недоступен; повторяю захват только с видео."));
        startProcess(false, false);
        return;
    }
    if (recording_ && exitCode != 0) {
        const QString details = captureProcess_ == nullptr ? QString() : QString::fromLocal8Bit(captureProcess_->readAll());
        emit error(QStringLiteral("Захват завершился с ошибкой: %1").arg(details.left(300)));
    }
    recording_ = false;
    segmentTimer_.stop();
    if (captureProcess_ != nullptr) {
        captureProcess_->deleteLater();
        captureProcess_ = nullptr;
    }
    emit stopped();
}

void PortableSegmentRecorder::processError(const QProcess::ProcessError errorCode) {
    // A failed audio input is expected to be recoverable: processFinished()
    // immediately retries the same capture without audio.
    if (recording_ && processHasAudio_ && !attemptedVideoOnlyFallback_) {
        return;
    }
    Q_UNUSED(errorCode)
    emit error(QStringLiteral("Не удалось запустить FFmpeg для захвата экрана."));
}

QString PortableSegmentRecorder::locateFfmpeg() const {
    const QString adjacent = QCoreApplication::applicationDirPath() + "/ffmpeg.exe";
    if (QFileInfo::exists(adjacent)) {
        return adjacent;
    }
    return QStandardPaths::findExecutable(QStringLiteral("ffmpeg"));
}

QStringList PortableSegmentRecorder::captureArguments(const bool withAudio) const {
    const auto monitors = Platform::MonitorEnumerator::enumerate();
    const int monitorIndex = Platform::MonitorEnumerator::indexForId(
        monitors, QString::fromStdString(settings_.capture.monitorId));
    const Platform::MonitorInfo monitor = monitors.value(std::max(0, monitorIndex));
    const int fps = std::clamp(settings_.capture.fps, 15, std::max(15, monitor.refreshRate));
    QRect captureRect = monitor.geometry;
    if (settings_.capture.source == "custom_region" && settings_.capture.regionWidth > 3 &&
        settings_.capture.regionHeight > 3) {
        const QRect localRegion(settings_.capture.regionX, settings_.capture.regionY,
                                settings_.capture.regionWidth, settings_.capture.regionHeight);
        const QRect monitorLocal(QPoint(0, 0), monitor.geometry.size());
        const QRect boundedRegion = localRegion.intersected(monitorLocal);
        if (boundedRegion.width() > 3 && boundedRegion.height() > 3) {
            captureRect = QRect(monitor.geometry.topLeft() + boundedRegion.topLeft(), boundedRegion.size());
        }
    }
    const int outputWidth = settings_.capture.outputWidth > 0 ? settings_.capture.outputWidth : captureRect.width();
    const int outputHeight = settings_.capture.outputHeight > 0 ? settings_.capture.outputHeight : captureRect.height();
    // The first portable MVP uses gdigrab because it is available in the
    // redistributable FFmpeg build and works on the current Windows test host.
    // DXGI Desktop Duplication / Windows Graphics Capture will replace this
    // process adapter in the native capture slice without changing the UI.
    QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("warning"),
        QStringLiteral("-y"), QStringLiteral("-f"), QStringLiteral("gdigrab"),
        QStringLiteral("-framerate"), QString::number(fps),
        QStringLiteral("-draw_mouse"), settings_.capture.showCursor ? QStringLiteral("1") : QStringLiteral("0"),
        QStringLiteral("-offset_x"), QString::number(captureRect.x()),
        QStringLiteral("-offset_y"), QString::number(captureRect.y()),
        QStringLiteral("-video_size"), QStringLiteral("%1x%2").arg(captureRect.width()).arg(captureRect.height()),
        QStringLiteral("-i"), QStringLiteral("desktop"),
    };
    if (withAudio && settings_.audio.systemEnabled) {
        args << QStringLiteral("-thread_queue_size") << QStringLiteral("512")
             << QStringLiteral("-f") << QStringLiteral("wasapi")
             << QStringLiteral("-loopback") << QStringLiteral("1")
             << QStringLiteral("-i") << QStringLiteral("default");
    }

    args << QStringLiteral("-map") << QStringLiteral("0:v:0");
    if (withAudio && settings_.audio.systemEnabled) {
        args << QStringLiteral("-map") << QStringLiteral("1:a:0");
    }
    const QString container = QString::fromStdString(settings_.video.container).toLower();
    QString codec = QString::fromStdString(settings_.video.codec).toLower();
    if (codec == QStringLiteral("auto")) {
        codec = container == QStringLiteral("webm") ? QStringLiteral("libvpx-vp9") : QStringLiteral("h264_nvenc");
    }
    if (container == QStringLiteral("webm") && codec != QStringLiteral("libvpx-vp9")) {
        codec = QStringLiteral("libvpx-vp9");
    }
    if (codec != QStringLiteral("h264_nvenc") && codec != QStringLiteral("libx264") &&
        codec != QStringLiteral("libvpx-vp9")) {
        codec = QStringLiteral("h264_nvenc");
    }
    QString preset = QString::fromStdString(settings_.video.preset).toLower();
    const int bitrate = preset == QStringLiteral("low")      ? 6000
                        : preset == QStringLiteral("medium") ? 10000
                        : preset == QStringLiteral("ultra")  ? 24000
                        : settings_.video.customBitrateKbps;
    args << QStringLiteral("-vf") << QStringLiteral("scale=%1:%2:flags=lanczos").arg(outputWidth).arg(outputHeight)
         << QStringLiteral("-c:v") << codec
         << QStringLiteral("-b:v") << QStringLiteral("%1k").arg(bitrate);
    if (codec == QStringLiteral("h264_nvenc")) {
        args << QStringLiteral("-preset")
             << (preset == QStringLiteral("low") ? QStringLiteral("p7")
                 : preset == QStringLiteral("ultra") ? QStringLiteral("p1") : QStringLiteral("p5"));
    } else if (codec == QStringLiteral("libx264")) {
        args << QStringLiteral("-preset")
             << (preset == QStringLiteral("low") ? QStringLiteral("veryfast")
                 : preset == QStringLiteral("ultra") ? QStringLiteral("slow") : QStringLiteral("medium"));
    } else {
        args << QStringLiteral("-deadline") << QStringLiteral("good")
             << QStringLiteral("-cpu-used") << (preset == QStringLiteral("low") ? QStringLiteral("6")
                                                  : preset == QStringLiteral("ultra") ? QStringLiteral("2")
                                                                                       : QStringLiteral("4"));
    }
    args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
         << QStringLiteral("-force_key_frames") << QStringLiteral("expr:gte(t,n_forced*1)");
    if (withAudio && settings_.audio.systemEnabled) {
        args << QStringLiteral("-c:a")
             << (container == QStringLiteral("webm") ? QStringLiteral("libopus") : QStringLiteral("aac"))
             << QStringLiteral("-ar") << QString::number(settings_.audio.sampleRate)
             << QStringLiteral("-ac") << QStringLiteral("2");
    } else {
        args << QStringLiteral("-an");
    }
    args << QStringLiteral("-f") << QStringLiteral("segment")
         << QStringLiteral("-segment_time") << QStringLiteral("1")
         << QStringLiteral("-break_non_keyframes") << QStringLiteral("1")
         << QStringLiteral("-reset_timestamps") << QStringLiteral("1")
         << QStringLiteral("-segment_format") << QStringLiteral("matroska")
         << QStringLiteral("-segment_start_number") << QString::number(nextSegmentNumber())
         << (segmentDirectory_ + "/segment_%06d.mkv");
    return args;
}

QStringList PortableSegmentRecorder::segmentFiles() const {
    if (segmentDirectory_.isEmpty()) {
        return {};
    }
    const QDir directory(segmentDirectory_);
    QStringList files;
    for (const QFileInfo& info : directory.entryInfoList({QStringLiteral("segment_*.mkv")}, QDir::Files,
                                                         QDir::Name)) {
        files.push_back(info.absoluteFilePath());
    }
    return files;
}

int PortableSegmentRecorder::nextSegmentNumber() const {
    int next = 0;
    const QDir directory(segmentDirectory_);
    for (const QFileInfo& info : directory.entryInfoList({QStringLiteral("segment_*.mkv")}, QDir::Files,
                                                          QDir::Name)) {
        const QString stem = info.completeBaseName();
        bool ok = false;
        const int number = stem.mid(QStringLiteral("segment_").size()).toInt(&ok);
        if (ok) {
            next = std::max(next, number + 1);
        }
    }
    return next;
}

QString PortableSegmentRecorder::escapeConcatPath(const QString& path) const {
    QString escaped = path;
    return escaped.replace('\\', '/').replace('"', "\\\"").replace('\'', "'\\''");
}

QString PortableSegmentRecorder::selectedMonitorLabel() const {
    const auto monitors = Platform::MonitorEnumerator::enumerate();
    const int index = Platform::MonitorEnumerator::indexForId(
        monitors, QString::fromStdString(settings_.capture.monitorId));
    return monitors.value(std::max(0, index)).name;
}

void PortableSegmentRecorder::startProcess(const bool withAudio, const bool announceStarted) {
    if (captureProcess_ != nullptr) {
        captureProcess_->disconnect(this);
        captureProcess_->terminate();
        captureProcess_->deleteLater();
    }
    processHasAudio_ = withAudio;
    captureProcess_ = new QProcess(this);
    captureProcess_->setProcessChannelMode(QProcess::MergedChannels);
    connect(captureProcess_, &QProcess::finished, this, &PortableSegmentRecorder::processFinished);
    connect(captureProcess_, &QProcess::errorOccurred, this, &PortableSegmentRecorder::processError);
    captureProcess_->start(ffmpegPath_, captureArguments(withAudio));
    if (!captureProcess_->waitForStarted(3000)) {
        emit error(QStringLiteral("FFmpeg не запустился для монитора %1.").arg(selectedMonitorLabel()));
        captureProcess_->deleteLater();
        captureProcess_ = nullptr;
        recording_ = false;
        return;
    }
    recording_ = true;
    paused_ = false;
    segmentTimer_.start();
    if (announceStarted) {
        emit started();
    }
}

void PortableSegmentRecorder::finishExport(ExportJob* job, const int exitCode,
                                            const QProcess::ExitStatus status) {
    if (job == nullptr) {
        return;
    }
    const QString output = job->process == nullptr ? QString() : QString::fromLocal8Bit(job->process->readAll());
    const bool processOk = exitCode == 0 && status == QProcess::NormalExit;
    if (processOk && QFileInfo::exists(job->temporaryPath)) {
        if (settings_.video.maxFileSizeMiB > 0 &&
            QFileInfo(job->temporaryPath).size() > static_cast<qint64>(settings_.video.maxFileSizeMiB) * 1024 * 1024) {
            QFile::remove(job->temporaryPath);
            QFile::remove(job->finalPath);
            emit error(QStringLiteral("Клип превысил установленный лимит размера файла."));
        } else if (QFile::remove(job->finalPath) && QFile::rename(job->temporaryPath, job->finalPath)) {
            emit clipSaved(job->finalPath);
        } else {
            emit error(QStringLiteral("Не удалось атомарно переименовать готовый клип."));
        }
    } else {
        QFile::remove(job->temporaryPath);
        QFile::remove(job->finalPath);
        emit error(QStringLiteral("Экспорт клипа завершился ошибкой: %1").arg(output.left(300)));
    }
    if (job->listFile != nullptr) {
        job->listFile->close();
        job->listFile->remove();
        job->listFile->deleteLater();
    } else {
        QFile::remove(job->listPath);
    }
    if (job->process != nullptr) {
        job->process->deleteLater();
    }
    exports_.removeOne(job);
    delete job;
}

} // namespace LastFrame::Media
