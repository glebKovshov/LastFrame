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
    void setCorner(const QString& corner);
    void setOpacity(double opacity);

protected:
    void paintEvent(QPaintEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;

private:
    QTimer hideTimer_;
    QString text_;
    QRect monitorGeometry_;
    QString corner_ = QStringLiteral("top_right");
    bool error_ = false;

    void reposition();
};

} // namespace LastFrame::UI
