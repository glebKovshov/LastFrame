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
    setFixedSize(380, 76);
    hideTimer_.setSingleShot(true);
    connect(&hideTimer_, &QTimer::timeout, this, &NotificationOverlay::hide);
}

void NotificationOverlay::showMessage(const QString& text, const bool error, const int durationMs) {
    text_ = text;
    error_ = error;
    reposition();
    hideTimer_.start(std::max(500, durationMs));
    show();
    raise();
    update();
}

void NotificationOverlay::setMonitorGeometry(const QRect& geometry) {
    monitorGeometry_ = geometry;
    reposition();
}

void NotificationOverlay::setCorner(const QString& corner) {
    if (corner == QStringLiteral("top_left") || corner == QStringLiteral("top_right") ||
        corner == QStringLiteral("bottom_left") || corner == QStringLiteral("bottom_right")) {
        corner_ = corner;
    } else {
        corner_ = QStringLiteral("top_right");
    }
    reposition();
}

void NotificationOverlay::setOpacity(const double opacity) {
    setWindowOpacity(std::clamp(opacity, 0.20, 1.0));
}

void NotificationOverlay::reposition() {
    if (monitorGeometry_.isEmpty()) {
        return;
    }
    constexpr int margin = 20;
    const bool left = corner_ == QStringLiteral("top_left") || corner_ == QStringLiteral("bottom_left");
    const bool bottom = corner_ == QStringLiteral("bottom_left") || corner_ == QStringLiteral("bottom_right");
    const int x = left ? monitorGeometry_.left() + margin : monitorGeometry_.right() - width() - margin;
    const int y = bottom ? monitorGeometry_.bottom() - height() - margin : monitorGeometry_.top() + margin;
    move(x, y);
}

void NotificationOverlay::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF panel = rect().adjusted(1, 1, -1, -1);
    painter.setPen(QPen(QColor(QStringLiteral("#ED760E")), 1));
    painter.setBrush(QColor(QStringLiteral("#242424E6")));
    painter.drawRoundedRect(panel, 12, 12);

    const QColor marker = error_ ? QColor(QStringLiteral("#D94841")) : QColor(QStringLiteral("#ED760E"));
    painter.setPen(Qt::NoPen);
    painter.setBrush(marker);
    painter.drawEllipse(QPointF(20, height() / 2.0), 5, 5);

    painter.setPen(Qt::white);
    painter.setFont(QFont(QStringLiteral("Segoe UI"), 10));
    painter.drawText(QRect(38, 10, width() - 52, height() - 20), Qt::AlignVCenter | Qt::TextWordWrap, text_);
}

void NotificationOverlay::showEvent(QShowEvent* event) {
    QWidget::showEvent(event);
#if defined(Q_OS_WIN)
    // Windows 10 2004+ excludes this helper window from supported capture APIs.
    SetWindowDisplayAffinity(reinterpret_cast<HWND>(winId()), WDA_EXCLUDEFROMCAPTURE);
#endif
}

void NotificationOverlay::hideEvent(QHideEvent* event) {
    QWidget::hideEvent(event);
}

} // namespace LastFrame::UI
