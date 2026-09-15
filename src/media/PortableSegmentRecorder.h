#pragma once

#include "core/RateLimiter.h"
#include "core/Settings.h"

#include <QProcess>
#include <QList>
#include <QTimer>

#include <chrono>
#include <memory>

class QTemporaryFile;

namespace LastFrame::Platform {
class WindowsDesktopCapture;
class WindowsAudioCapture;
class WindowsGraphicsCapture;
}

namespace LastFrame::Media {

class PortableSegmentRecorder final : public QObject {
    Q_OBJECT

public:
    explicit PortableSegmentRecorder(QObject* parent = nullptr);
    ~PortableSegmentRecorder() override;

    [[nodiscard]] bool isRecording() const noexcept { return recording_; }
    [[nodiscard]] bool isPaused() const noexcept { return paused_; }
    [[nodiscard]] bool isMicrophoneMuted() const noexcept { return microphoneMuted_; }
    [[nodiscard]] QString ffmpegPath() const noexcept { return ffmpegPath_; }
    [[nodiscard]] QString temporaryDirectory() const noexcept { return segmentDirectory_; }

public slots:
    void start(const LastFrame::Core::Settings& settings);
    void pause();
    void resume();
    void stop();
    void clearBuffer();
    void saveClip();
    void setMicrophoneMuted(bool muted);

signals:
    void started();
    void pausedChanged(bool paused);
    void stopped();
    void clipSaved(const QString& path);
    void message(const QString& text);
    void error(const QString& text);
    void microphoneMuteChanged(bool muted);

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
    [[nodiscard]] bool prepareNativeCapture();
    [[nodiscard]] bool prepareWindowsGraphicsCapture();
    [[nodiscard]] bool prepareNativeAudio();
    void discoverMicrophoneDevice();
    [[nodiscard]] bool tryNextAutomaticEncoder();
    void recoverNativeCapture(const QString& reason);
    void recoverNativeAudio(const QString& reason);
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
    bool captureSystemAudio_ = true;
    QString microphoneDeviceName_;
    bool useDesktopDuplication_ = true;
    bool attemptedDesktopDuplicationFallback_ = false;
    bool useNativeCapture_ = false;
    bool attemptedNativeCaptureFallback_ = false;
    int nativeCaptureRecoveryAttempts_ = 0;
    bool useWindowsGraphicsCapture_ = false;
    bool attemptedWindowsGraphicsCaptureFallback_ = false;
    bool useNativeAudio_ = false;
    bool attemptedNativeAudioFallback_ = false;
    int nativeAudioRecoveryAttempts_ = 0;
    bool microphoneMuted_ = false;
    bool useSoftwareEncoder_ = false;
    bool attemptedEncoderFallback_ = false;
    int automaticEncoderAttempt_ = 0;
    LastFrame::Core::RateLimiter rateLimiter_{3, std::chrono::seconds(1), std::chrono::seconds(5)};
    QList<ExportJob*> exports_;
#if defined(Q_OS_WIN)
    std::unique_ptr<LastFrame::Platform::WindowsDesktopCapture> nativeCapture_;
    std::unique_ptr<LastFrame::Platform::WindowsAudioCapture> nativeAudio_;
    std::unique_ptr<LastFrame::Platform::WindowsGraphicsCapture> windowsGraphicsCapture_;
#endif
};

} // namespace LastFrame::Media
