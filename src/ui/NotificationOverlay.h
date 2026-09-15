#pragma once

#include <QTimer>
#include <QWidget>
#include <QRect>
#include <QString>

class QPaintEvent;
class QShowEvent;
class QHideEvent;

namespace LastFrame::UI {

class NotificationOverlay final : public QWidget {
    Q_OBJECT

public:
    explicit NotificationOverlay(QWidget* parent = nullptr);
    ~NotificationOverlay() override = default;

    void showMessage(const QString& text, bool error, int durationMs = 3500);
    void setMonitorGeometry(const QRect& geometry);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QTimer animationTimer_;
    QTimer hideTimer_;
    QString text_;
    QRect monitorGeometry_;
    bool error_ = false;
    int animationAngle_ = 0;
};

} // namespace LastFrame::UI
