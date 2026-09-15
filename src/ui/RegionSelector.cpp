#include "ui/RegionSelector.h"

#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QScreen>
#include <QApplication>

#include <algorithm>

namespace LastFrame::UI {

RegionSelector::RegionSelector(const QRect& monitorGeometry, QWidget* parent)
    : QDialog(parent), monitorGeometry_(monitorGeometry) {
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_DeleteOnClose, false);
    setModal(true);
    setCursor(Qt::CrossCursor);
    setGeometry(monitorGeometry_);
}

QRect RegionSelector::normalizedSelection() const {
    return selection_.normalized().intersected(rect().adjusted(1, 1, -1, -1));
}

void RegionSelector::keyPressEvent(QKeyEvent* event) {
    if (event->key() == Qt::Key_Escape) {
        reject();
        return;
    }
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) {
        const QRect selection = normalizedSelection();
        if (selection.width() >= 4 && selection.height() >= 4) {
            selection_ = selection;
            accept();
            return;
        }
    }
    QDialog::keyPressEvent(event);
}

void RegionSelector::mousePressEvent(QMouseEvent* event) {
    if (event->button() == Qt::RightButton) {
        reject();
        return;
    }
    if (event->button() != Qt::LeftButton) {
        return;
    }
    anchor_ = event->position().toPoint();
    selection_ = QRect(anchor_, anchor_);
    selecting_ = true;
    update();
}

void RegionSelector::mouseMoveEvent(QMouseEvent* event) {
    if (!selecting_) {
        return;
    }
    selection_ = QRect(anchor_, event->position().toPoint()).normalized();
    update();
}

void RegionSelector::mouseReleaseEvent(QMouseEvent* event) {
    if (!selecting_ || event->button() != Qt::LeftButton) {
        return;
    }
    selecting_ = false;
    selection_ = normalizedSelection();
    if (selection_.width() >= 4 && selection_.height() >= 4) {
        accept();
    } else {
        update();
    }
}

void RegionSelector::paintEvent(QPaintEvent* event) {
    Q_UNUSED(event)
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.fillRect(rect(), QColor(14, 16, 20, 178));

    const QRect selection = normalizedSelection();
    if (selection.width() >= 1 && selection.height() >= 1) {
        painter.setCompositionMode(QPainter::CompositionMode_Clear);
        painter.fillRect(selection, Qt::transparent);
        painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
        painter.fillRect(selection, QColor(237, 118, 14, 42));
        painter.setPen(QPen(QColor(QStringLiteral("#ED760E")), 2));
        painter.drawRect(selection);

        const QString sizeLabel = QStringLiteral("%1 × %2  |  %3, %4")
                                      .arg(selection.width())
                                      .arg(selection.height())
                                      .arg(selection.x())
                                      .arg(selection.y());
        const QRect labelRect(selection.topLeft() + QPoint(8, 8), QSize(190, 28));
        painter.fillRect(labelRect, QColor(24, 24, 24, 225));
        painter.setPen(Qt::white);
        painter.drawText(labelRect, Qt::AlignCenter, sizeLabel);
    }

    painter.setPen(Qt::white);
    painter.setBrush(QColor(24, 24, 24, 220));
    const QRect hintRect(18, 18, 390, 34);
    painter.drawRoundedRect(hintRect, 8, 8);
    painter.drawText(hintRect, Qt::AlignCenter,
                     QStringLiteral("Перетащите область · Enter — применить · Esc/ПКМ — отмена"));
}

} // namespace LastFrame::UI
