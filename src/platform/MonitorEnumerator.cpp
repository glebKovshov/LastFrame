#include "platform/MonitorEnumerator.h"

#include <QGuiApplication>
#include <QScreen>

#if defined(Q_OS_WIN)
#include <Windows.h>
#include <vector>
#endif

namespace LastFrame::Platform {

#if defined(Q_OS_WIN)
namespace {

struct NativeMonitorLookup {
    QString name;
    QRect geometry;
};

BOOL CALLBACK findNativeMonitorGeometry(HMONITOR monitor, HDC, LPRECT, LPARAM data) {
    auto* lookup = reinterpret_cast<NativeMonitorLookup*>(data);
    MONITORINFOEXW info{};
    info.cbSize = sizeof(info);
    if (lookup == nullptr || !GetMonitorInfoW(monitor, &info)) {
        return TRUE;
    }
    if (QString::fromWCharArray(info.szDevice) == lookup->name) {
        lookup->geometry = QRect(info.rcMonitor.left, info.rcMonitor.top,
                                 info.rcMonitor.right - info.rcMonitor.left,
                                 info.rcMonitor.bottom - info.rcMonitor.top);
        return FALSE;
    }
    return TRUE;
}

QRect nativeGeometryForScreen(const QString& screenName, const QRect& logicalGeometry, const qreal dpr) {
    NativeMonitorLookup lookup{screenName, {}};
    EnumDisplayMonitors(nullptr, nullptr, findNativeMonitorGeometry, reinterpret_cast<LPARAM>(&lookup));
    if (lookup.geometry.isValid()) {
        return lookup.geometry;
    }
    const qreal scale = dpr > 0.0 ? dpr : 1.0;
    return QRect(qRound(logicalGeometry.x() * scale), qRound(logicalGeometry.y() * scale),
                 qRound(logicalGeometry.width() * scale), qRound(logicalGeometry.height() * scale));
}

bool isHdrEnabled(const QString& screenName) {
    UINT pathCount = 0;
    UINT modeCount = 0;
    if (GetDisplayConfigBufferSizes(QDC_ONLY_ACTIVE_PATHS, &pathCount, &modeCount) != ERROR_SUCCESS ||
        pathCount == 0) {
        return false;
    }
    std::vector<DISPLAYCONFIG_PATH_INFO> paths(pathCount);
    std::vector<DISPLAYCONFIG_MODE_INFO> modes(modeCount);
    if (QueryDisplayConfig(QDC_ONLY_ACTIVE_PATHS, &pathCount, paths.data(), &modeCount, modes.data(), nullptr) !=
        ERROR_SUCCESS) {
        return false;
    }
    for (const auto& path : paths) {
        DISPLAYCONFIG_SOURCE_DEVICE_NAME source{};
        source.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_SOURCE_NAME;
        source.header.size = sizeof(source);
        source.header.adapterId = path.sourceInfo.adapterId;
        source.header.id = path.sourceInfo.id;
        if (DisplayConfigGetDeviceInfo(&source.header) != ERROR_SUCCESS ||
            QString::fromWCharArray(source.viewGdiDeviceName) != screenName) {
            continue;
        }
        DISPLAYCONFIG_GET_ADVANCED_COLOR_INFO color{};
        color.header.type = DISPLAYCONFIG_DEVICE_INFO_GET_ADVANCED_COLOR_INFO;
        color.header.size = sizeof(color);
        color.header.adapterId = path.targetInfo.adapterId;
        color.header.id = path.targetInfo.id;
        return DisplayConfigGetDeviceInfo(&color.header) == ERROR_SUCCESS && color.advancedColorEnabled != FALSE;
    }
    return false;
}

} // namespace
#endif

QVector<MonitorInfo> MonitorEnumerator::enumerate() {
    QVector<MonitorInfo> result;
    const auto screens = QGuiApplication::screens();
    result.reserve(screens.size());
    for (const QScreen* screen : screens) {
        const QRect geometry = screen->geometry();
        MonitorInfo monitor;
        const QString screenName = screen->name().trimmed();
        const QString serial = screen->serialNumber().trimmed();
        monitor.id = QStringLiteral("display:%1%2")
                         .arg(screenName.isEmpty() ? QStringLiteral("unknown") : screenName,
                              serial.isEmpty() ? QString() : QStringLiteral(":%1").arg(serial));
        monitor.name = screen->name().isEmpty() ? QStringLiteral("Display") : screen->name();
        monitor.geometry = geometry;
        monitor.resolution = screen->size();
        monitor.devicePixelRatio = screen->devicePixelRatio();
#if defined(Q_OS_WIN)
        monitor.nativeGeometry = nativeGeometryForScreen(screenName, geometry, monitor.devicePixelRatio);
#else
        monitor.nativeGeometry = geometry;
#endif
        monitor.nativeResolution = monitor.nativeGeometry.size();
        monitor.refreshRate = qRound(screen->refreshRate());
        monitor.orientation = screen->orientation() == Qt::LandscapeOrientation
                                  ? QStringLiteral("Landscape")
                                  : QStringLiteral("Portrait");
#if defined(Q_OS_WIN)
        monitor.hdrEnabled = isHdrEnabled(screenName);
#endif
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
    return -1;
}

} // namespace LastFrame::Platform
