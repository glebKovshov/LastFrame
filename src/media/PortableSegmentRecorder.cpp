#include "media/PortableSegmentRecorder.h"

#include "core/FilenameAllocator.h"
#include "platform/AudioDeviceEnumerator.h"
#include "platform/MonitorEnumerator.h"
#if defined(Q_OS_WIN)
#include "platform/WindowsDesktopCapture.h"
#include "platform/WindowsAudioCapture.h"
#include "platform/WindowsGraphicsCapture.h"
#endif

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QTemporaryFile>
#include <QtConcurrent/QtConcurrentRun>

#include <chrono>
#include <algorithm>
#include <utility>

namespace LastFrame::Media {

namespace {

bool materializeSegments(const QList<QPair<QString, QByteArray>>& segments) {
    for (const auto& segment : segments) {
        const QString& path = segment.first;
        if (QFileInfo::exists(path)) {
            continue;
        }
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(segment.second) != segment.second.size()) {
            file.close();
            return false;
        }
        file.close();
    }
    return true;
}

} // namespace

PortableSegmentRecorder::PortableSegmentRecorder(QObject* parent) : QObject(parent) {
#if defined(Q_OS_WIN)
    nativeCapture_ = std::make_unique<Platform::WindowsDesktopCapture>(this);
    connect(nativeCapture_.get(), &Platform::WindowsDesktopCapture::failed, this, [this](const QString& reason) {
        if (!recording_ || !useNativeCapture_) {
            return;
        }
        recoverNativeCapture(reason);
    });
    windowsGraphicsCapture_ = std::make_unique<Platform::WindowsGraphicsCapture>(this);
    connect(windowsGraphicsCapture_.get(), &Platform::WindowsGraphicsCapture::failed, this, [this](const QString& reason) {
        if (!recording_ || !useWindowsGraphicsCapture_) {
            return;
        }
        attemptedWindowsGraphicsCaptureFallback_ = true;
        useWindowsGraphicsCapture_ = false;
        windowsGraphicsCapture_->stop();
        emit message(QStringLiteral("[capture_failed] Windows Graphics Capture недоступен; возвращаюсь к FFmpeg backend: %1")
                         .arg(reason));
        if (captureProcess_ != nullptr) {
            QProcess* process = captureProcess_;
            process->disconnect(this);
            process->terminate();
            if (!process->waitForFinished(1000)) {
                process->kill();
                process->waitForFinished(1000);
            }
            process->deleteLater();
            captureProcess_ = nullptr;
        }
        startProcess(processHasAudio_, false);
    });
    nativeAudio_ = std::make_unique<Platform::WindowsAudioCapture>(this);
    connect(nativeAudio_.get(), &Platform::WindowsAudioCapture::degraded, this, [this](const QString& reason) {
        emit message(QStringLiteral("[audio_device_lost] Native audio source degraded: %1").arg(reason));
    });
    connect(nativeAudio_.get(), &Platform::WindowsAudioCapture::failed, this, [this](const QString& reason) {
        if (!recording_ || !useNativeAudio_) {
            return;
        }
        recoverNativeAudio(reason);
    });
#endif
    segmentTimer_.setInterval(500);
    connect(&segmentTimer_, &QTimer::timeout, this, &PortableSegmentRecorder::reapSegments);
}

PortableSegmentRecorder::~PortableSegmentRecorder() {
    stop();
    for (ExportJob* job : std::as_const(exports_)) {
        if (job->materializer != nullptr) {
            job->materializer->future().waitForFinished();
            delete job->materializer;
            job->materializer = nullptr;
        }
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
        emit error(QStringLiteral("[encoder_unavailable] FFmpeg не найден. Поместите ffmpeg.exe рядом с LastFrame.exe или добавьте его в PATH."));
        return;
    }
    const auto monitors = Platform::MonitorEnumerator::enumerate();
    const QString selectedMonitorId = QString::fromStdString(settings_.capture.monitorId);
    if (monitors.isEmpty() ||
        (!selectedMonitorId.isEmpty() && selectedMonitorId.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0 &&
         Platform::MonitorEnumerator::indexForId(monitors, selectedMonitorId) < 0)) {
        emit error(QStringLiteral("[capture_failed] Выбранный монитор недоступен. Обновите список мониторов и выберите его заново."));
        return;
    }

    segmentDirectory_ = settings_.buffer.temporaryDirectory.empty()
                            ? QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation) + "/segments"
                            : QString::fromStdString(settings_.buffer.temporaryDirectory.string());
    if (!QDir().mkpath(segmentDirectory_)) {
        emit error(QStringLiteral("[disk_full] Не удалось создать временный каталог сегментов: %1").arg(segmentDirectory_));
        return;
    }
    const QStorageInfo temporaryStorage(segmentDirectory_);
    if (!temporaryStorage.isValid() || !temporaryStorage.isReady() || temporaryStorage.isReadOnly() ||
        temporaryStorage.bytesAvailable() < 64LL * 1024LL * 1024LL) {
        emit error(QStringLiteral("[disk_full] Недостаточно доступного локального диска для временных сегментов."));
        return;
    }
    const QDir directory(segmentDirectory_);
    for (auto it = ramSegments_.begin(); it != ramSegments_.end();) {
        if (snapshotReferences_.contains(it.key())) {
            ++it;
        } else {
            ramSegmentBytes_ -= it.value().size();
            it = ramSegments_.erase(it);
        }
    }
    for (const QFileInfo& file : directory.entryInfoList({QStringLiteral("segment_*.mkv"),
                                                           QStringLiteral("concat-*.txt"),
                                                           QStringLiteral("*.tmp")},
                                                           QDir::Files)) {
        if (!snapshotReferences_.contains(file.absoluteFilePath())) {
            QFile::remove(file.absoluteFilePath());
        }
    }
    attemptedVideoOnlyFallback_ = false;
    captureSystemAudio_ = settings_.audio.systemEnabled;
    discoverMicrophoneDevice();
    useDesktopDuplication_ = true;
    attemptedDesktopDuplicationFallback_ = false;
    useNativeCapture_ = false;
    attemptedNativeCaptureFallback_ = false;
    nativeCaptureRecoveryAttempts_ = 0;
    useWindowsGraphicsCapture_ = false;
    attemptedWindowsGraphicsCaptureFallback_ = false;
    useNativeAudio_ = false;
    attemptedNativeAudioFallback_ = false;
    nativeAudioRecoveryAttempts_ = 0;
#if defined(Q_OS_WIN)
    useNativeCapture_ = prepareNativeCapture();
    const bool forceWindowsGraphicsCapture = qEnvironmentVariable("LASTFRAME_FORCE_WGC") == QStringLiteral("1");
    if (forceWindowsGraphicsCapture) {
        useNativeCapture_ = false;
        attemptedNativeCaptureFallback_ = true;
    }
    if (!useNativeCapture_) {
        useWindowsGraphicsCapture_ = prepareWindowsGraphicsCapture();
    }
    useNativeAudio_ = prepareNativeAudio();
#endif
    useSoftwareEncoder_ = false;
    attemptedEncoderFallback_ = false;
    automaticEncoderAttempt_ = 0;
    paused_ = false;
    startProcess(true);
}

void PortableSegmentRecorder::pause() {
    if (!recording_ || paused_) {
        return;
    }
    recording_ = false;
    if (captureProcess_ != nullptr) {
#if defined(Q_OS_WIN)
        if (useNativeCapture_) {
            nativeCapture_->stop();
        }
        if (useWindowsGraphicsCapture_) {
            windowsGraphicsCapture_->stop();
        }
        if (useNativeAudio_) {
            nativeAudio_->stop();
        }
#endif
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
    automaticEncoderAttempt_ = 0;
    captureSystemAudio_ = settings_.audio.systemEnabled;
    discoverMicrophoneDevice();
#if defined(Q_OS_WIN)
    useNativeCapture_ = !attemptedNativeCaptureFallback_ && prepareNativeCapture();
    if (useNativeCapture_) {
        nativeCaptureRecoveryAttempts_ = 0;
    }
    useWindowsGraphicsCapture_ = !useNativeCapture_ && !attemptedWindowsGraphicsCaptureFallback_ &&
                                 prepareWindowsGraphicsCapture();
    useNativeAudio_ = !attemptedNativeAudioFallback_ && prepareNativeAudio();
    if (useNativeAudio_) {
        nativeAudioRecoveryAttempts_ = 0;
    }
#endif
    startProcess(true);
    emit pausedChanged(false);
}

void PortableSegmentRecorder::stop() {
    const bool wasActive = recording_ || paused_;
    recording_ = false;
#if defined(Q_OS_WIN)
    if (nativeCapture_ != nullptr) {
        nativeCapture_->stop();
    }
    if (windowsGraphicsCapture_ != nullptr) {
        windowsGraphicsCapture_->stop();
    }
    if (nativeAudio_ != nullptr) {
        nativeAudio_->stop();
    }
#endif
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
    const QStringList files = segmentFiles();
    const int removableCount = recording_ && !files.isEmpty() ? static_cast<int>(files.size()) - 1
                                                               : static_cast<int>(files.size());
    for (int index = 0; index < removableCount; ++index) {
        const QString& file = files.at(index);
        if (!snapshotReferences_.contains(file)) {
            QFile::remove(file);
        }
    }
    for (auto it = ramSegments_.begin(); it != ramSegments_.end();) {
        if (snapshotReferences_.contains(it.key())) {
            ++it;
        } else {
            ramSegmentBytes_ -= it.value().size();
            it = ramSegments_.erase(it);
        }
    }
    emit message(QStringLiteral("Буфер очищен; сохранённые клипы не затронуты."));
}

void PortableSegmentRecorder::saveClip() {
    using namespace std::chrono;
    if (rateLimiter_.tryAccept(steady_clock::now()) == Core::RateLimitResult::Cooldown) {
        emit error(QStringLiteral("[rate_limit] Слишком много сохранений. Новые снимки временно заблокированы на 5 секунд."));
        return;
    }

    QStringList files = segmentFiles();
    // The segment muxer keeps the newest file open while FFmpeg is running.
    // Never hand that incomplete Matroska file to the concat demuxer.
    if (recording_ && files.size() > 1) {
        files.removeLast();
    }
    if (files.isEmpty()) {
        emit error(QStringLiteral("[buffer_empty] В буфере пока нет готовых сегментов."));
        return;
    }
    if (exports_.size() >= settings_.storage.maxQueueLength) {
        emit error(QStringLiteral("[queue_overflow] Очередь экспорта заполнена. Дождитесь завершения предыдущих клипов."));
        return;
    }

    const QString clipsDirectory = settings_.storage.clipsDirectory.empty()
                                       ? QStandardPaths::writableLocation(QStandardPaths::MoviesLocation) + "/LastFrame"
                                       : QString::fromStdString(settings_.storage.clipsDirectory.string());
    if (!QDir().mkpath(clipsDirectory)) {
        emit error(QStringLiteral("[disk_full] Не удалось создать каталог клипов: %1").arg(clipsDirectory));
        return;
    }
    const QStorageInfo clipStorage(clipsDirectory);
    if (!clipStorage.isValid() || !clipStorage.isReady() || clipStorage.isReadOnly() ||
        clipStorage.bytesAvailable() < 64LL * 1024LL * 1024LL) {
        emit error(QStringLiteral("[disk_full] Недостаточно доступного локального диска для экспорта клипа."));
        return;
    }
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
        emit error(QStringLiteral("[export_failed] Не удалось зарезервировать имя итогового клипа."));
        return;
    }
    reservation.close();

    auto* job = new ExportJob;
    job->temporaryPath = QString::fromStdString(target.string());
    job->temporaryPath += ".tmp";
    job->finalPath = finalPath;
    job->container = container;
    job->snapshotFiles = files;
    retainSnapshot(job->snapshotFiles);
    QFile::remove(job->temporaryPath);
    exports_.push_back(job);

    QList<QPair<QString, QByteArray>> materialization;
    for (const QString& file : files) {
        const auto cached = ramSegments_.constFind(file);
        if (cached != ramSegments_.cend()) {
            materialization.append(qMakePair(file, cached.value()));
        }
    }
    if (materialization.isEmpty()) {
        beginExport(job);
    } else {
        job->materializer = new QFutureWatcher<bool>(this);
        connect(job->materializer, &QFutureWatcher<bool>::finished, this, [this, job] {
            const bool success = job->materializer->future().result();
            job->materializer->deleteLater();
            job->materializer = nullptr;
            if (!success) {
                QFile::remove(job->temporaryPath);
                QFile::remove(job->finalPath);
                releaseSnapshot(job->snapshotFiles);
                exports_.removeOne(job);
                delete job;
                emit error(QStringLiteral("[export_failed] Не удалось подготовить RAM-сегменты для экспорта."));
                return;
            }
            beginExport(job);
        });
        job->materializer->setFuture(QtConcurrent::run([materialization = std::move(materialization)] {
            return materializeSegments(materialization);
        }));
    }
    emit message(QStringLiteral("Экспорт клипа поставлен в очередь."));
}

void PortableSegmentRecorder::beginExport(ExportJob* job) {
    if (job == nullptr) {
        return;
    }
    auto* listFile = new QTemporaryFile(segmentDirectory_ + "/concat-XXXXXX.txt", this);
    listFile->setAutoRemove(false);
    if (!listFile->open()) {
        QFile::remove(job->finalPath);
        releaseSnapshot(job->snapshotFiles);
        exports_.removeOne(job);
        listFile->deleteLater();
        delete job;
        emit error(QStringLiteral("[export_failed] Не удалось создать список сегментов для экспорта."));
        return;
    }
    QByteArray content("ffconcat version 1.0\n");
    for (const QString& file : std::as_const(job->snapshotFiles)) {
        content += "file '" + escapeConcatPath(file).toUtf8() + "'\n";
        // Every closed capture segment is cut on a one-second boundary. The
        // explicit duration gives the concat demuxer a monotonic timeline
        // when Matroska packet timestamps restart at zero per segment.
        content += "duration 1.0\n";
    }
    if (listFile->write(content) != content.size()) {
        listFile->close();
        listFile->remove();
        listFile->deleteLater();
        QFile::remove(job->finalPath);
        releaseSnapshot(job->snapshotFiles);
        exports_.removeOne(job);
        delete job;
        emit error(QStringLiteral("[export_failed] Не удалось записать список сегментов для экспорта."));
        return;
    }
    listFile->close();

    job->listFile = listFile;
    job->listPath = listFile->fileName();
    job->process = new QProcess(this);
    job->process->setProcessChannelMode(QProcess::MergedChannels);
    connect(job->process, &QProcess::finished, this,
            [this, job](int code, QProcess::ExitStatus status) { finishExport(job, code, status); });
    connect(job->process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
        emit error(QStringLiteral("[export_failed] FFmpeg не смог запустить экспорт клипа."));
    });

    QStringList args{
        QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("error"),
        QStringLiteral("-fflags"), QStringLiteral("+genpts"),
        QStringLiteral("-f"), QStringLiteral("concat"), QStringLiteral("-safe"), QStringLiteral("0"),
        QStringLiteral("-i"), job->listPath,
    };
    if (job->container == QStringLiteral("mkv")) {
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
    if (job->container == QStringLiteral("mp4")) {
        args << QStringLiteral("-movflags") << QStringLiteral("+faststart");
    }
    const qint64 maxBytes = static_cast<qint64>(settings_.video.maxFileSizeMiB) * 1024 * 1024;
    if (maxBytes > 0) {
        args << QStringLiteral("-fs") << QString::number(maxBytes);
    }
    args << QStringLiteral("-f")
         << (job->container == QStringLiteral("mkv") ? QStringLiteral("matroska") : job->container)
         << job->temporaryPath;
    job->process->start(ffmpegPath_, args);
}

void PortableSegmentRecorder::setMicrophoneMuted(const bool muted) {
    if (microphoneMuted_ == muted) {
        return;
    }
    microphoneMuted_ = muted;
#if defined(Q_OS_WIN)
    if (useNativeAudio_ && nativeAudio_ != nullptr) {
        nativeAudio_->setMicrophoneMuted(muted);
    } else if (recording_ && processHasAudio_) {
        QProcess* process = captureProcess_;
        if (process != nullptr) {
            process->disconnect(this);
            process->terminate();
            if (!process->waitForFinished(1000)) {
                process->kill();
                process->waitForFinished(1000);
            }
            process->deleteLater();
            captureProcess_ = nullptr;
        }
        startProcess(true, false);
    }
#endif
    emit microphoneMuteChanged(muted);
}

void PortableSegmentRecorder::reapSegments() {
    promoteClosedSegmentsToRam();
    evictOldSegments();
}

void PortableSegmentRecorder::processFinished(const int exitCode, const QProcess::ExitStatus status) {
    Q_UNUSED(status)
    const QString processOutput = captureProcess_ == nullptr ? QString() : QString::fromLocal8Bit(captureProcess_->readAll());
    if (recording_ && exitCode != 0 && processHasAudio_ && !attemptedVideoOnlyFallback_) {
#if defined(Q_OS_WIN)
        if (useNativeCapture_) {
            nativeCapture_->stop();
        }
        if (useNativeAudio_) {
            nativeAudio_->stop();
            useNativeAudio_ = false;
            attemptedNativeAudioFallback_ = true;
        }
#endif
        if (captureSystemAudio_ && !attemptedNativeAudioFallback_) {
            captureSystemAudio_ = false;
            emit message(QStringLiteral("[audio_device_lost] Системный звук недоступен; продолжаю с доступным микрофоном или видео. %1")
                             .arg(processOutput.left(240).simplified()));
            startProcess(true, false);
            return;
        }
        attemptedVideoOnlyFallback_ = true;
        emit message(QStringLiteral("[audio_device_lost] Аудио недоступно; повторяю захват только с видео. %1")
                         .arg(processOutput.left(240).simplified()));
        startProcess(false, false);
        return;
    }
#if defined(Q_OS_WIN)
    if (recording_ && exitCode != 0 && useDesktopDuplication_ && !attemptedDesktopDuplicationFallback_) {
        attemptedDesktopDuplicationFallback_ = true;
        useDesktopDuplication_ = false;
        emit message(QStringLiteral("Desktop Duplication недоступен; использую совместимый GDI-захват."));
        startProcess(processHasAudio_, false);
        return;
    }
#endif
    if (recording_ && exitCode != 0 && tryNextAutomaticEncoder()) {
        startProcess(processHasAudio_, false);
        return;
    }
    if (recording_ && exitCode != 0 && !useSoftwareEncoder_ && !attemptedEncoderFallback_ &&
        (settings_.video.container != "webm") &&
        (settings_.video.codec == "auto" || settings_.video.codec == "h264_nvenc" ||
         settings_.video.codec == "h264_amf" || settings_.video.codec == "h264_qsv" ||
         settings_.video.codec == "h264_videotoolbox")) {
        attemptedEncoderFallback_ = true;
        useSoftwareEncoder_ = true;
        emit message(QStringLiteral("Аппаратный H.264 недоступен; использую software fallback."));
        startProcess(processHasAudio_, false);
        return;
    }
    if (recording_ && exitCode != 0) {
        emit error(QStringLiteral("[capture_failed] Захват завершился с ошибкой: %1").arg(processOutput.left(300)));
    }
    recording_ = false;
#if defined(Q_OS_WIN)
    if (nativeCapture_ != nullptr) {
        nativeCapture_->stop();
    }
    if (windowsGraphicsCapture_ != nullptr) {
        windowsGraphicsCapture_->stop();
    }
    if (nativeAudio_ != nullptr) {
        nativeAudio_->stop();
    }
#endif
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
    emit error(QStringLiteral("[capture_failed] Не удалось запустить FFmpeg для захвата экрана."));
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
    const int localOffsetX = captureRect.x() - monitor.geometry.x();
    const int localOffsetY = captureRect.y() - monitor.geometry.y();
    QStringList args{QStringLiteral("-hide_banner"), QStringLiteral("-loglevel"), QStringLiteral("warning"),
                     QStringLiteral("-y")};
#if defined(Q_OS_WIN)
    if (useNativeCapture_) {
        const QSize nativeFrameSize = nativeCapture_->frameSize();
        args << QStringLiteral("-f") << QStringLiteral("rawvideo") << QStringLiteral("-pixel_format")
             << QStringLiteral("bgra") << QStringLiteral("-video_size")
             << QStringLiteral("%1x%2").arg(nativeFrameSize.width()).arg(nativeFrameSize.height())
             << QStringLiteral("-framerate") << QString::number(fps) << QStringLiteral("-i")
             << nativeCapture_->inputPath();
    } else if (useWindowsGraphicsCapture_) {
        const QSize nativeFrameSize = windowsGraphicsCapture_->frameSize();
        args << QStringLiteral("-f") << QStringLiteral("rawvideo") << QStringLiteral("-pixel_format")
             << QStringLiteral("bgra") << QStringLiteral("-video_size")
             << QStringLiteral("%1x%2").arg(nativeFrameSize.width()).arg(nativeFrameSize.height())
             << QStringLiteral("-framerate") << QString::number(fps) << QStringLiteral("-i")
             << windowsGraphicsCapture_->inputPath();
    } else
#endif
#if defined(Q_OS_WIN)
    if (useDesktopDuplication_) {
        const QString filter = QStringLiteral("ddagrab=output_idx=%1:draw_mouse=%2:framerate=%3:video_size=%4x%5:offset_x=%6:offset_y=%7:output_fmt=bgra")
                                   .arg(std::max(0, monitorIndex))
                                   .arg(settings_.capture.showCursor ? 1 : 0)
                                   .arg(fps)
                                   .arg(captureRect.width())
                                   .arg(captureRect.height())
                                   .arg(localOffsetX)
                                   .arg(localOffsetY);
        args << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i") << filter;
    } else {
        args << QStringLiteral("-f") << QStringLiteral("gdigrab") << QStringLiteral("-framerate")
             << QString::number(fps) << QStringLiteral("-draw_mouse")
             << (settings_.capture.showCursor ? QStringLiteral("1") : QStringLiteral("0"))
             << QStringLiteral("-offset_x") << QString::number(captureRect.x()) << QStringLiteral("-offset_y")
             << QString::number(captureRect.y()) << QStringLiteral("-video_size")
             << QStringLiteral("%1x%2").arg(captureRect.width()).arg(captureRect.height()) << QStringLiteral("-i")
             << QStringLiteral("desktop");
    }
#elif defined(Q_OS_MAC)
    const QString avfoundationScreen = qEnvironmentVariable("LASTFRAME_AVFOUNDATION_SCREEN", QStringLiteral("1"));
    args << QStringLiteral("-f") << QStringLiteral("avfoundation") << QStringLiteral("-framerate")
         << QString::number(fps) << QStringLiteral("-capture_cursor")
         << (settings_.capture.showCursor ? QStringLiteral("1") : QStringLiteral("0"))
         << QStringLiteral("-i") << (avfoundationScreen + QStringLiteral(":none"));
#elif defined(Q_OS_LINUX)
    args << QStringLiteral("-f") << QStringLiteral("x11grab") << QStringLiteral("-framerate")
         << QString::number(fps) << QStringLiteral("-video_size")
         << QStringLiteral("%1x%2").arg(captureRect.width()).arg(captureRect.height()) << QStringLiteral("-i")
         << QStringLiteral(":0.0+%1,%2").arg(captureRect.x()).arg(captureRect.y());
#else
    args << QStringLiteral("-f") << QStringLiteral("lavfi") << QStringLiteral("-i")
         << QStringLiteral("color=size=%1x%2:rate=%3")
                .arg(captureRect.width()).arg(captureRect.height()).arg(fps);
#endif
#if defined(Q_OS_WIN)
    const bool includeNativeAudio = withAudio && useNativeAudio_;
    const bool includeSystemAudio = withAudio && !includeNativeAudio && settings_.audio.systemEnabled && captureSystemAudio_;
    const bool includeMicrophone = withAudio && !includeNativeAudio && !microphoneMuted_ &&
                                   settings_.audio.microphoneEnabled && !microphoneDeviceName_.isEmpty();
#else
    const bool includeNativeAudio = false;
    const bool includeSystemAudio = false;
    const bool includeMicrophone = false;
#endif
    int systemInputIndex = -1;
    int microphoneInputIndex = -1;
    int nativeAudioInputIndex = -1;
    int nextInputIndex = 1;
    if (includeNativeAudio) {
        nativeAudioInputIndex = nextInputIndex++;
#if defined(Q_OS_WIN)
        args << QStringLiteral("-thread_queue_size") << QStringLiteral("512")
             << QStringLiteral("-f") << QStringLiteral("f32le")
             << QStringLiteral("-ar") << QString::number(settings_.audio.sampleRate)
             << QStringLiteral("-ac") << QStringLiteral("2") << QStringLiteral("-i")
             << nativeAudio_->inputPath();
#endif
    } else if (includeSystemAudio) {
        systemInputIndex = nextInputIndex++;
        args << QStringLiteral("-thread_queue_size") << QStringLiteral("512")
             << QStringLiteral("-f") << QStringLiteral("wasapi")
             << QStringLiteral("-loopback") << QStringLiteral("1")
             << QStringLiteral("-i")
             << (settings_.audio.systemDeviceId == "auto"
                     ? QStringLiteral("default")
                     : QString::fromStdString(settings_.audio.systemDeviceId));
    }
    if (includeMicrophone) {
        microphoneInputIndex = nextInputIndex++;
        args << QStringLiteral("-thread_queue_size") << QStringLiteral("512")
             << QStringLiteral("-f") << QStringLiteral("dshow") << QStringLiteral("-i")
             << QStringLiteral("audio=%1").arg(microphoneDeviceName_);
    }

    args << QStringLiteral("-map") << QStringLiteral("0:v:0");
    const QString container = QString::fromStdString(settings_.video.container).toLower();
    QString codec = QString::fromStdString(settings_.video.codec).toLower();
    if (codec == QStringLiteral("auto")) {
        if (container == QStringLiteral("webm")) {
            codec = QStringLiteral("libvpx-vp9");
        } else {
            const QStringList hardwareEncoders{QStringLiteral("h264_nvenc"), QStringLiteral("h264_amf"),
                                               QStringLiteral("h264_qsv"), QStringLiteral("h264_videotoolbox")};
            codec = hardwareEncoders.value(automaticEncoderAttempt_, QStringLiteral("h264_nvenc"));
        }
    }
    if (useSoftwareEncoder_ && container != QStringLiteral("webm")) {
        codec = QStringLiteral("libx264");
    }
    if (container == QStringLiteral("webm") && codec != QStringLiteral("libvpx-vp9")) {
        codec = QStringLiteral("libvpx-vp9");
    }
    if (codec != QStringLiteral("h264_nvenc") && codec != QStringLiteral("h264_amf") &&
        codec != QStringLiteral("h264_qsv") && codec != QStringLiteral("h264_videotoolbox") &&
        codec != QStringLiteral("libx264") && codec != QStringLiteral("libvpx-vp9")) {
        codec = QStringLiteral("h264_nvenc");
    }
    QString preset = QString::fromStdString(settings_.video.preset).toLower();
    const int bitrate = preset == QStringLiteral("low")      ? 6000
                        : preset == QStringLiteral("medium") ? 10000
                        : preset == QStringLiteral("ultra")  ? 24000
                        : settings_.video.customBitrateKbps;
    const bool rawNativeCapture = useNativeCapture_ || useWindowsGraphicsCapture_;
#if defined(Q_OS_WIN)
    const bool desktopDuplicationInput = useDesktopDuplication_ && !rawNativeCapture;
#else
    const bool desktopDuplicationInput = false;
#endif
    const QString videoFilter = desktopDuplicationInput
                                    ? QStringLiteral("hwdownload,format=bgra,scale=%1:%2:flags=lanczos")
                                    : QStringLiteral("scale=%1:%2:flags=lanczos");
    args << QStringLiteral("-vf") << videoFilter.arg(outputWidth).arg(outputHeight)
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
    } else if (codec == QStringLiteral("libvpx-vp9")) {
        args << QStringLiteral("-deadline") << QStringLiteral("good")
             << QStringLiteral("-cpu-used") << (preset == QStringLiteral("low") ? QStringLiteral("6")
                                                  : preset == QStringLiteral("ultra") ? QStringLiteral("2")
                                                                                       : QStringLiteral("4"));
    }
    args << QStringLiteral("-pix_fmt") << QStringLiteral("yuv420p")
         << QStringLiteral("-force_key_frames") << QStringLiteral("expr:gte(t,n_forced*1)");
    if (includeNativeAudio) {
        args << QStringLiteral("-map") << QStringLiteral("%1:a:0").arg(nativeAudioInputIndex)
             << QStringLiteral("-c:a")
             << (container == QStringLiteral("webm") ? QStringLiteral("libopus") : QStringLiteral("aac"));
    } else if (includeSystemAudio && includeMicrophone) {
        const QString systemVolume = QString::number(settings_.audio.systemVolume, 'f', 3);
        const QString microphoneVolume = QString::number(settings_.audio.microphoneVolume, 'f', 3);
        args << QStringLiteral("-filter_complex")
             << QStringLiteral("[%1:a:0]volume=%2[system];[%3:a:0]volume=%4[microphone];[system][microphone]amix=inputs=2:duration=longest:dropout_transition=0[aout]")
                    .arg(systemInputIndex)
                    .arg(systemVolume)
                    .arg(microphoneInputIndex)
                    .arg(microphoneVolume)
             << QStringLiteral("-map") << QStringLiteral("[aout]")
             << QStringLiteral("-c:a")
             << (container == QStringLiteral("webm") ? QStringLiteral("libopus") : QStringLiteral("aac"))
             << QStringLiteral("-ar") << QString::number(settings_.audio.sampleRate) << QStringLiteral("-ac")
             << QStringLiteral("2");
    } else if (includeSystemAudio || includeMicrophone) {
        const int inputIndex = includeSystemAudio ? systemInputIndex : microphoneInputIndex;
        const QString volume = QString::number(includeSystemAudio ? settings_.audio.systemVolume
                                                                   : settings_.audio.microphoneVolume,
                                                'f', 3);
        args << QStringLiteral("-filter_complex") << QStringLiteral("[%1:a:0]volume=%2[aout]").arg(inputIndex).arg(volume)
             << QStringLiteral("-map") << QStringLiteral("[aout]") << QStringLiteral("-c:a")
             << (container == QStringLiteral("webm") ? QStringLiteral("libopus") : QStringLiteral("aac"))
             << QStringLiteral("-ar") << QString::number(settings_.audio.sampleRate) << QStringLiteral("-ac")
             << QStringLiteral("2");
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
    const QString currentDirectory = QDir(segmentDirectory_).absolutePath();
    for (auto it = ramSegments_.cbegin(); it != ramSegments_.cend(); ++it) {
        if (QFileInfo(it.key()).absolutePath() == currentDirectory) {
            files.push_back(it.key());
        }
    }
    files.removeDuplicates();
    std::sort(files.begin(), files.end());
    return files;
}

void PortableSegmentRecorder::promoteClosedSegmentsToRam() {
    if (!recording_ || !exports_.isEmpty() || segmentDirectory_.isEmpty()) {
        return;
    }
    const QStringList files = segmentFiles();
    if (files.size() < 2) {
        return;
    }
    const qint64 softLimit = std::max<qint64>(1, static_cast<qint64>(settings_.buffer.ramLimitMiB) * 1024 * 1024 * 70 / 100);
    for (int index = files.size() - 2; index >= 0; --index) {
        const QString& path = files.at(index);
        if (ramSegments_.contains(path)) {
            // A RAM segment may have been materialized temporarily for an
            // export. Once all exports are done, remove that duplicate and
            // return to the hybrid retention layout.
            if (QFileInfo::exists(path)) {
                QFile::remove(path);
            }
            continue;
        }
        QFile file(path);
        const qint64 size = QFileInfo(path).size();
        if (size <= 0 || ramSegmentBytes_ + size > softLimit || !file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QByteArray data = file.readAll();
        file.close();
        if (data.size() != size) {
            continue;
        }
        ramSegments_.insert(path, data);
        ramSegmentBytes_ += data.size();
        QFile::remove(path);
    }
}

void PortableSegmentRecorder::evictOldSegments() {
    const QStringList files = segmentFiles();
    const int keepCount = std::max(3, settings_.buffer.durationSeconds + 2);
    const int removeCount = files.size() > keepCount ? static_cast<int>(files.size()) - keepCount : 0;
    for (int index = 0; index < removeCount; ++index) {
        const QString& path = files.at(index);
        if (snapshotReferences_.contains(path)) {
            continue;
        }
        const auto cached = ramSegments_.find(path);
        if (cached != ramSegments_.end()) {
            ramSegmentBytes_ -= cached.value().size();
            ramSegments_.erase(cached);
        }
        QFile::remove(path);
    }
}

void PortableSegmentRecorder::retainSnapshot(const QStringList& files) {
    for (const QString& file : files) {
        ++snapshotReferences_[file];
    }
}

void PortableSegmentRecorder::releaseSnapshot(const QStringList& files) {
    for (const QString& file : files) {
        const auto it = snapshotReferences_.find(file);
        if (it == snapshotReferences_.end()) {
            continue;
        }
        if (--it.value() <= 0) {
            snapshotReferences_.erase(it);
        }
    }
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
    if (index >= 0 && index < monitors.size()) {
        return monitors.at(index).name;
    }
    return monitors.isEmpty() ? QStringLiteral("неизвестен") : monitors.first().name;
}

bool PortableSegmentRecorder::tryNextAutomaticEncoder() {
    if (useSoftwareEncoder_ || settings_.video.container == "webm" || settings_.video.codec != "auto" ||
        automaticEncoderAttempt_ >= 3) {
        return false;
    }
    ++automaticEncoderAttempt_;
    const QStringList names{QStringLiteral("NVENC"), QStringLiteral("AMD AMF"), QStringLiteral("Intel QSV"),
                            QStringLiteral("Apple VideoToolbox")};
    emit message(QStringLiteral("Hardware H.264 backend недоступен; пробую %1.").arg(names.value(automaticEncoderAttempt_)));
    return true;
}

void PortableSegmentRecorder::recoverNativeCapture(const QString& reason) {
#if defined(Q_OS_WIN)
    if (!recording_) {
        return;
    }
    useNativeCapture_ = false;
    nativeCapture_->stop();
    if (useNativeAudio_) {
        nativeAudio_->stop();
        useNativeAudio_ = false;
        attemptedNativeAudioFallback_ = true;
    }
    if (captureProcess_ != nullptr) {
        QProcess* process = captureProcess_;
        process->disconnect(this);
        process->terminate();
        if (!process->waitForFinished(1000)) {
            process->kill();
            process->waitForFinished(1000);
        }
        process->deleteLater();
        captureProcess_ = nullptr;
    }
    if (nativeCaptureRecoveryAttempts_ < 3) {
        const int delays[] = {250, 500, 1000};
        const int delay = delays[nativeCaptureRecoveryAttempts_];
        ++nativeCaptureRecoveryAttempts_;
        emit message(QStringLiteral("[capture_failed] Потеряна native DXGI-сессия; повтор %1/3 через %2 ms.")
                         .arg(nativeCaptureRecoveryAttempts_)
                         .arg(delay));
        QTimer::singleShot(delay, this, [this, reason] {
            if (!recording_) {
                return;
            }
            if (prepareNativeCapture()) {
                useNativeCapture_ = true;
                startProcess(processHasAudio_, false);
            } else {
                recoverNativeCapture(reason);
            }
        });
        return;
    }
    attemptedNativeCaptureFallback_ = true;
    if (!attemptedWindowsGraphicsCaptureFallback_ && prepareWindowsGraphicsCapture()) {
        useWindowsGraphicsCapture_ = true;
        emit message(QStringLiteral("DXGI Desktop Duplication не восстановился; переключаюсь на Windows Graphics Capture."));
        startProcess(processHasAudio_, false);
        return;
    }
    attemptedWindowsGraphicsCaptureFallback_ = true;
    emit message(QStringLiteral("[capture_failed] Native capture не восстановился; возвращаюсь к FFmpeg backend: %1")
                     .arg(reason));
    startProcess(processHasAudio_, false);
#else
    Q_UNUSED(reason)
#endif
}

void PortableSegmentRecorder::recoverNativeAudio(const QString& reason) {
#if defined(Q_OS_WIN)
    if (!recording_) {
        return;
    }
    useNativeAudio_ = false;
    nativeAudio_->stop();
    if (useNativeCapture_) {
        nativeCapture_->stop();
    }
    if (useWindowsGraphicsCapture_) {
        windowsGraphicsCapture_->stop();
    }
    if (captureProcess_ != nullptr) {
        QProcess* process = captureProcess_;
        process->disconnect(this);
        process->terminate();
        if (!process->waitForFinished(1000)) {
            process->kill();
            process->waitForFinished(1000);
        }
        process->deleteLater();
        captureProcess_ = nullptr;
    }
    if (nativeAudioRecoveryAttempts_ < 3) {
        const int delays[] = {250, 500, 1000};
        const int delay = delays[nativeAudioRecoveryAttempts_];
        ++nativeAudioRecoveryAttempts_;
        emit message(QStringLiteral("[audio_device_lost] Потеряна native WASAPI-сессия; повтор %1/3 через %2 ms.")
                         .arg(nativeAudioRecoveryAttempts_)
                         .arg(delay));
        QTimer::singleShot(delay, this, [this, reason] {
            if (!recording_) {
                return;
            }
            if (prepareNativeAudio()) {
                useNativeAudio_ = true;
                startProcess(processHasAudio_, false);
            } else {
                recoverNativeAudio(reason);
            }
        });
        return;
    }
    attemptedNativeAudioFallback_ = true;
    emit message(QStringLiteral("[audio_device_lost] Native WASAPI не восстановился; возвращаюсь к FFmpeg audio backend: %1")
                     .arg(reason));
    startProcess(processHasAudio_, false);
#else
    Q_UNUSED(reason)
#endif
}

bool PortableSegmentRecorder::prepareNativeCapture() {
#if defined(Q_OS_WIN)
    if (nativeCapture_ == nullptr) {
        return false;
    }
    const auto monitors = Platform::MonitorEnumerator::enumerate();
    const int monitorIndex = Platform::MonitorEnumerator::indexForId(
        monitors, QString::fromStdString(settings_.capture.monitorId));
    if (monitorIndex < 0 || monitorIndex >= monitors.size()) {
        return false;
    }
    const Platform::MonitorInfo monitor = monitors.at(monitorIndex);
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
    Platform::WindowsDesktopCapture::Config config;
    config.outputIndex = monitorIndex;
    config.monitorGeometry = monitor.geometry;
    config.captureGeometry = captureRect;
    config.fps = std::clamp(settings_.capture.fps, 15, std::max(15, monitor.refreshRate));
    config.showCursor = settings_.capture.showCursor;
    QString errorText;
    if (!nativeCapture_->prepare(config, &errorText)) {
        emit message(QStringLiteral("Нативный DXGI backend недоступен, использую FFmpeg backend: %1")
                         .arg(errorText));
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool PortableSegmentRecorder::prepareWindowsGraphicsCapture() {
#if defined(Q_OS_WIN)
    if (windowsGraphicsCapture_ == nullptr) {
        return false;
    }
    const auto monitors = Platform::MonitorEnumerator::enumerate();
    const int monitorIndex = Platform::MonitorEnumerator::indexForId(
        monitors, QString::fromStdString(settings_.capture.monitorId));
    if (monitorIndex < 0 || monitorIndex >= monitors.size()) {
        return false;
    }
    const Platform::MonitorInfo monitor = monitors.at(monitorIndex);
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
    Platform::WindowsGraphicsCapture::Config config;
    config.monitorGeometry = monitor.geometry;
    config.captureGeometry = captureRect;
    config.fps = std::clamp(settings_.capture.fps, 15, std::max(15, monitor.refreshRate));
    config.showCursor = settings_.capture.showCursor;
    QString errorText;
    if (!windowsGraphicsCapture_->prepare(config, &errorText)) {
        emit message(QStringLiteral("Windows Graphics Capture backend недоступен, использую FFmpeg backend: %1")
                         .arg(errorText));
        return false;
    }
    return true;
#else
    return false;
#endif
}

bool PortableSegmentRecorder::prepareNativeAudio() {
#if defined(Q_OS_WIN)
    if (nativeAudio_ == nullptr || (!settings_.audio.systemEnabled && !settings_.audio.microphoneEnabled)) {
        return false;
    }
    Platform::WindowsAudioCapture::Config config;
    config.systemEnabled = settings_.audio.systemEnabled;
    config.systemDeviceId = QString::fromStdString(settings_.audio.systemDeviceId);
    config.microphoneEnabled = settings_.audio.microphoneEnabled;
    config.microphoneDeviceId = QString::fromStdString(settings_.audio.microphoneDeviceId);
    config.systemVolume = settings_.audio.systemVolume;
    config.microphoneVolume = settings_.audio.microphoneVolume;
    config.microphoneMuted = microphoneMuted_;
    config.sampleRate = settings_.audio.sampleRate;
    QString errorText;
    if (!nativeAudio_->prepare(config, &errorText)) {
        emit message(QStringLiteral("Native WASAPI backend недоступен, использую FFmpeg audio backend: %1")
                         .arg(errorText));
        return false;
    }
    return true;
#else
    return false;
#endif
}

void PortableSegmentRecorder::discoverMicrophoneDevice() {
    microphoneDeviceName_.clear();
    if (!settings_.audio.microphoneEnabled) {
        return;
    }
#if defined(Q_OS_WIN)
    const QString requested = QString::fromStdString(settings_.audio.microphoneDeviceId).trimmed();
    if (!requested.isEmpty() && requested.compare(QStringLiteral("auto"), Qt::CaseInsensitive) != 0) {
        microphoneDeviceName_ = requested;
        return;
    }
    const auto devices = Platform::AudioDeviceEnumerator::microphones(ffmpegPath_);
    if (!devices.isEmpty()) {
        microphoneDeviceName_ = devices.first().id;
    }
#else
    Q_UNUSED(ffmpegPath_)
#endif
}

void PortableSegmentRecorder::startProcess(const bool withAudio, const bool announceStarted) {
#if defined(Q_OS_WIN)
    if (captureProcess_ != nullptr) {
        if (useNativeCapture_) {
            nativeCapture_->stop();
        }
        if (useWindowsGraphicsCapture_) {
            windowsGraphicsCapture_->stop();
        }
        if (useNativeAudio_) {
            nativeAudio_->stop();
        }
    }
    if (!withAudio) {
        useNativeAudio_ = false;
    }
    if (useNativeCapture_ && !nativeCapture_->isPrepared()) {
        if (!prepareNativeCapture()) {
            useNativeCapture_ = false;
            attemptedNativeCaptureFallback_ = true;
        }
    }
    if (useWindowsGraphicsCapture_ && !windowsGraphicsCapture_->isPrepared()) {
        if (!prepareWindowsGraphicsCapture()) {
            useWindowsGraphicsCapture_ = false;
            attemptedWindowsGraphicsCaptureFallback_ = true;
        }
    }
    if (withAudio && useNativeAudio_ && !nativeAudio_->isPrepared()) {
        if (!prepareNativeAudio()) {
            useNativeAudio_ = false;
            attemptedNativeAudioFallback_ = true;
        }
    }
#endif
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
#if defined(Q_OS_WIN)
        if (useNativeAudio_) {
            nativeAudio_->stop();
            useNativeAudio_ = false;
            attemptedNativeAudioFallback_ = true;
            emit message(QStringLiteral("Native WASAPI backend не запустился; использую FFmpeg audio backend."));
            captureProcess_->deleteLater();
            captureProcess_ = nullptr;
            startProcess(withAudio, announceStarted);
            return;
        }
        if (useNativeCapture_) {
            nativeCapture_->stop();
            useNativeCapture_ = false;
            attemptedNativeCaptureFallback_ = true;
            emit message(QStringLiteral("Нативный DXGI backend не запустился; использую FFmpeg capture backend."));
            captureProcess_->deleteLater();
            captureProcess_ = nullptr;
            startProcess(withAudio, announceStarted);
            return;
        }
        if (useWindowsGraphicsCapture_) {
            windowsGraphicsCapture_->stop();
            useWindowsGraphicsCapture_ = false;
            attemptedWindowsGraphicsCaptureFallback_ = true;
            emit message(QStringLiteral("Windows Graphics Capture не запустился; использую FFmpeg capture backend."));
            captureProcess_->deleteLater();
            captureProcess_ = nullptr;
            startProcess(withAudio, announceStarted);
            return;
        }
#endif
#if defined(Q_OS_WIN)
        const bool canFallbackToGdi = useDesktopDuplication_ && !attemptedDesktopDuplicationFallback_;
#else
        const bool canFallbackToGdi = false;
#endif
        emit error(QStringLiteral("[capture_failed] FFmpeg не запустился для монитора %1.").arg(selectedMonitorLabel()));
        captureProcess_->deleteLater();
        captureProcess_ = nullptr;
        if (canFallbackToGdi) {
            attemptedDesktopDuplicationFallback_ = true;
            useDesktopDuplication_ = false;
            emit message(QStringLiteral("Desktop Duplication недоступен; использую совместимый GDI-захват."));
            startProcess(withAudio, announceStarted);
            return;
        }
        if (tryNextAutomaticEncoder()) {
            startProcess(withAudio, announceStarted);
            return;
        }
        const bool canFallbackToSoftware = !useSoftwareEncoder_ && !attemptedEncoderFallback_ &&
                                           settings_.video.container != "webm" &&
                                           (settings_.video.codec == "auto" || settings_.video.codec == "h264_nvenc" ||
                                            settings_.video.codec == "h264_amf" || settings_.video.codec == "h264_qsv" ||
                                            settings_.video.codec == "h264_videotoolbox");
        if (canFallbackToSoftware) {
            attemptedEncoderFallback_ = true;
            useSoftwareEncoder_ = true;
            emit message(QStringLiteral("Аппаратный H.264 недоступен; использую software fallback."));
            startProcess(withAudio, announceStarted);
            return;
        }
        recording_ = false;
        return;
    }
#if defined(Q_OS_WIN)
    if (useNativeCapture_ && !nativeCapture_->start()) {
        nativeCapture_->stop();
        useNativeCapture_ = false;
        attemptedNativeCaptureFallback_ = true;
        if (!attemptedWindowsGraphicsCaptureFallback_ && prepareWindowsGraphicsCapture()) {
            useWindowsGraphicsCapture_ = true;
            emit message(QStringLiteral("Нативный DXGI backend не запустился; переключаюсь на Windows Graphics Capture."));
        } else {
            attemptedWindowsGraphicsCaptureFallback_ = true;
            emit message(QStringLiteral("Нативный DXGI backend не запустился; использую FFmpeg capture backend."));
        }
        captureProcess_->terminate();
        captureProcess_->waitForFinished(1000);
        captureProcess_->deleteLater();
        captureProcess_ = nullptr;
        startProcess(withAudio, announceStarted);
        return;
    }
    if (useWindowsGraphicsCapture_ && !windowsGraphicsCapture_->start()) {
        windowsGraphicsCapture_->stop();
        useWindowsGraphicsCapture_ = false;
        attemptedWindowsGraphicsCaptureFallback_ = true;
        emit message(QStringLiteral("Windows Graphics Capture не запустился; использую FFmpeg capture backend."));
        captureProcess_->terminate();
        captureProcess_->waitForFinished(1000);
        captureProcess_->deleteLater();
        captureProcess_ = nullptr;
        startProcess(withAudio, announceStarted);
        return;
    }
    if (withAudio && useNativeAudio_ && !nativeAudio_->start()) {
        nativeAudio_->stop();
        useNativeAudio_ = false;
        attemptedNativeAudioFallback_ = true;
        emit message(QStringLiteral("Native WASAPI backend не запустился; использую FFmpeg audio backend."));
        if (useNativeCapture_) {
            nativeCapture_->stop();
        }
        captureProcess_->terminate();
        captureProcess_->waitForFinished(1000);
        captureProcess_->deleteLater();
        captureProcess_ = nullptr;
        startProcess(withAudio, announceStarted);
        return;
    }
#endif
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
            emit error(QStringLiteral("[file_size_limit] Клип превысил установленный лимит размера файла."));
        } else if (QFile::remove(job->finalPath) && QFile::rename(job->temporaryPath, job->finalPath)) {
            emit clipSaved(job->finalPath);
        } else {
            emit error(QStringLiteral("[export_failed] Не удалось атомарно переименовать готовый клип."));
        }
    } else {
        QFile::remove(job->temporaryPath);
        QFile::remove(job->finalPath);
        emit error(QStringLiteral("[export_failed] Экспорт клипа завершился ошибкой: %1").arg(output.left(300)));
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
    releaseSnapshot(job->snapshotFiles);
    exports_.removeOne(job);
    delete job;
}

} // namespace LastFrame::Media
