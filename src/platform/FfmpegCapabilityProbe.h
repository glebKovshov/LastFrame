#pragma once

#include <QProcess>
#include <QTimer>

namespace LastFrame::Platform {

struct FfmpegCapabilities {
    QString path;
    QString version;
    bool available = false;
    bool desktopDuplication = false;
    bool gdiCapture = false;
    bool scaleCuda = false;
    bool scaleQsv = false;
    bool scaleVaapi = false;
    bool scaleVulkan = false;
    bool directShow = false;
    bool wasapi = false;
    bool nvencH264 = false;
    bool amfH264 = false;
    bool qsvH264 = false;
    bool videoToolboxH264 = false;
    bool softwareH264 = false;
    bool vp9 = false;
};

class FfmpegCapabilityProbe final : public QObject {
    Q_OBJECT

public:
    explicit FfmpegCapabilityProbe(QObject* parent = nullptr);

public slots:
    void probe(const QString& ffmpegPath);

signals:
    void finished(const LastFrame::Platform::FfmpegCapabilities& capabilities);

private slots:
    void handleFinished(int exitCode, QProcess::ExitStatus status);
    void handleError(QProcess::ProcessError error);

private:
    void runNextQuery();
    void parseQueryOutput(const QString& output);

    QProcess process_;
    QTimer timeout_;
    QString ffmpegPath_;
    QStringList queries_;
    int queryIndex_ = 0;
    bool queryActive_ = false;
    FfmpegCapabilities capabilities_;
};

} // namespace LastFrame::Platform

Q_DECLARE_METATYPE(LastFrame::Platform::FfmpegCapabilities)
