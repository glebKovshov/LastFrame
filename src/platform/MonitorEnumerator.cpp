#include "platform/MonitorEnumerator.h"

#include <QGuiApplication>
#include <QScreen>

namespace LastFrame::Platform {

QVector<MonitorInfo> MonitorEnumerator::enumerate() {
    QVector<MonitorInfo> result;
    const auto screens = QGuiApplication::screens();
    result.reserve(screens.size());
    for (const QScreen* screen : screens) {
        const QRect geometry = screen->geometry();
        MonitorInfo monitor;
        monitor.id = QStringLiteral("%1:%2,%3,%4x%5")
                         .arg(screen->name())
                         .arg(geometry.x())
                         .arg(geometry.y())
                         .arg(geometry.width())
                         .arg(geometry.height());
        monitor.name = screen->name().isEmpty() ? QStringLiteral("Display") : screen->name();
        monitor.geometry = geometry;
        monitor.resolution = screen->size();
        monitor.refreshRate = qRound(screen->refreshRate());
        monitor.devicePixelRatio = screen->devicePixelRatio();
        monitor.orientation = screen->orientation() == Qt::LandscapeOrientation
                                  ? QStringLiteral("Landscape")
                                  : QStringLiteral("Portrait");
        result.push_back(std::move(monitor));
    }
    return result;
}

int MonitorEnumerator::indexForId(const QVector<MonitorInfo>& monitors, const QString& id) {
    for (int index = 0; index < monitors.size(); ++index) {
        if (monitors.at(index).id == id) {
            return index;
        }
    }
    return monitors.isEmpty() ? -1 : 0;
}

} // namespace LastFrame::Platform
