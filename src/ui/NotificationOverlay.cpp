#include "ui/NotificationOverlay.h"

#include <QPainter>
#include <QPaintEvent>
#include <QShowEvent>
#include <QHideEvent>

#include <algorithm>

#if defined(Q_OS_WIN)
#include <Windows.h>
#endif

namespace LastFrame::UI {

NotificationOverlay::NotificationOverlay(QWidget* parent) : QWidget(parent) {
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::WindowStaysOnTopHint | Qt::WindowDoesNotAcceptFocus);
    setFixedSize(360, 82);
    animationTimer_.setInterval(80);
    animationTimer_.setSingleShot(false);
    hideTimer_.setSingleShot(true);
    connect(&animationTimer_, &QTimer::timeout, this, [this] {
        animationAngle_ = (animationAngle_ + 24) % 360;
        update();
    });
    connect(&hideTimer_, &QTimer::timeout, this, &NotificationOverlay::hide);
}

void NotificationOverlay::showMessage(const QString& text, const bool error, const int durationMs) {
    text_ = text;
    error_ = error;
    if (!monitorGeometry_.isEmpty()) {
        move(monitorGeometry_.right() - width() - 24, monitorGeometry_.top() + 24);
    }
    hideTimer_.start(std::max(500, durationMs));
    show();
    raise();
    animationTimer_.start();
    update();
}

void NotificationOverlay::setMonitorGeometry(const QRect& geometry) {
    monitorGeometry_ = geometry;
    if (isVisible() && !monitorGeometry_.isEmpty()) {
        move(monitorGeometry_.right() - width() - 24, monitorGeometry_.top() + 24);
    }
}

void NotificationOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF panel = rect().adjusted(1, 1, -1, -1);
    painter.setPen(QPen(QColor(QStringLiteral("#ED760E")), 1));
    painter.setBrush(QColor(QStringLiteral("#242424E6")));
    painter.drawRoundedRect(panel, 12, 12);

    const QPoint center(27, height() / 2);
    painter.setPen(QPen(QColor(QStringLiteral("#6E6E6E")), 4, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRect(center.x() - 11, center.y() - 11, 22, 22), 0, 360 * 16);
    painter.setPen(QPen(error_ ? QColor(QStringLiteral("#D94841")) : QColor(QStringLiteral("#ED760E")),
                       4, Qt::SolidLine, Qt::RoundCap));
    painter.drawArc(QRect(center.x() - 11, center.y() - 11, 22, 22), animationAngle_ * 16, 105 * 16);

    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 10));
    painter.drawText(QRect(52, 13, width() - 66, height() - 26), Qt::AlignVCenter | Qt::TextWordWrap, text_);
}

void NotificationOverlay::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
#if defined(Q_OS_WIN)
    // Windows 10 2004+ excludes this helper window from supported capture APIs.
    SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_EXCLUDEFROMCAPTURE);
#endif
}

void NotificationOverlay::hideEvent(QHideEvent* event) {
    animationTimer_.stop();
    QWidget::hideEvent(event);
}

} // namespace LastFrame::UI
