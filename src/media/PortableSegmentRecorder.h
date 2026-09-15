#pragma once

#include "core/RateLimiter.h"
#include "core/Settings.h"

#include <QProcess>
#include <QList>
#include <QTimer>

#include <chrono>

class QTemporaryFile;

namespace LastFrame::Media {

class PortableSegmentRecorder final : public QObject {
    Q_OBJECT

public:
    explicit PortableSegmentRecorder(QObject* parent = nullptr);
    ~PortableSegmentRecorder() override;

    [[nodiscard]] bool isRecording() const noexcept { return recording_; }
    [[nodiscard]] bool isPaused() const noexcept { return paused_; }
    [[nodiscard]] QString ffmpegPath() const noexcept { return ffmpegPath_; }
    [[nodiscard]] QString temporaryDirectory() const noexcept { return segmentDirectory_; }

public slots:
    void start(const LastFrame::Core::Settings& settings);
    void pause();
    void resume();
    void stop();
    void clearBuffer();
    void saveClip();

signals:
    void started();
    void pausedChanged(bool paused);
    void stopped();
    void clipSaved(const QString& path);
    void message(const QString& text);
    void error(const QString& text);

private slots:
    void reapSegments();
    void processFinished(int exitCode, QProcess::ExitStatus status);
    void processError(QProcess::ProcessError error);

private:
    struct ExportJob {
        QProcess* process = nullptr;
        QTemporaryFile* listFile = nullptr;
        QString listPath;
        QString temporaryPath;
        QString finalPath;
    };

    [[nodiscard]] QString locateFfmpeg() const;
    [[nodiscard]] QStringList captureArguments(bool withAudio) const;
    [[nodiscard]] QStringList segmentFiles() const;
    [[nodiscard]] int nextSegmentNumber() const;
    [[nodiscard]] QString escapeConcatPath(const QString& path) const;
    [[nodiscard]] QString selectedMonitorLabel() const;
    void startProcess(bool withAudio, bool announceStarted = true);
    void finishExport(ExportJob* job, int exitCode, QProcess::ExitStatus status);

    LastFrame::Core::Settings settings_;
    QString ffmpegPath_;
    QString segmentDirectory_;
    QProcess* captureProcess_ = nullptr;
    QTimer segmentTimer_;
    bool recording_ = false;
    bool paused_ = false;
    bool attemptedVideoOnlyFallback_ = false;
    bool processHasAudio_ = true;
    LastFrame::Core::RateLimiter rateLimiter_{3, std::chrono::seconds(1), std::chrono::seconds(5)};
    QList<ExportJob*> exports_;
};

} // namespace LastFrame::Media
