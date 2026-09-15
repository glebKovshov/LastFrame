#pragma once

#include <QObject>
#include <QString>

#include <memory>

namespace LastFrame::Platform {

class WindowsAudioCapture final : public QObject {
    Q_OBJECT

public:
    struct Config {
        bool systemEnabled = true;
        QString systemDeviceId = QStringLiteral("auto");
        bool microphoneEnabled = true;
        QString microphoneDeviceId = QStringLiteral("auto");
        double systemVolume = 1.0;
        double microphoneVolume = 1.0;
        int sampleRate = 48'000;
    };

    explicit WindowsAudioCapture(QObject* parent = nullptr);
    ~WindowsAudioCapture() override;

    [[nodiscard]] bool prepare(const Config& config, QString* error = nullptr);
    [[nodiscard]] bool start();
    void stop();

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] QString inputPath() const;

signals:
    void degraded(const QString& reason);
    void failed(const QString& reason);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace LastFrame::Platform
