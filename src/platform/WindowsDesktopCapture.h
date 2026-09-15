#pragma once

#include <QObject>
#include <QRect>
#include <QSize>
#include <QString>

#include <memory>

namespace LastFrame::Platform {

class WindowsDesktopCapture final : public QObject {
    Q_OBJECT

public:
    struct Config {
        int outputIndex = 0;
        QRect monitorGeometry;
        QRect captureGeometry;
        int fps = 60;
        bool showCursor = true;
    };

    explicit WindowsDesktopCapture(QObject* parent = nullptr);
    ~WindowsDesktopCapture() override;

    [[nodiscard]] bool prepare(const Config& config, QString* error = nullptr);
    [[nodiscard]] bool start();
    void stop();

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] QString inputPath() const;
    [[nodiscard]] QSize frameSize() const noexcept;

signals:
    void failed(const QString& reason);

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace LastFrame::Platform
