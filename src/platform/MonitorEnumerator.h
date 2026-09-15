#pragma once

#include <QRect>
#include <QSize>
#include <QString>
#include <QVector>

namespace LastFrame::Platform {

struct MonitorInfo {
    QString id;
    QString name;
    QRect geometry;
    QSize resolution;
    // Qt geometry is used by the UI and follows the desktop's logical/DPI
    // coordinate space. Native capture APIs need physical desktop pixels.
    QRect nativeGeometry;
    QSize nativeResolution;
    int refreshRate = 60;
    qreal devicePixelRatio = 1.0;
    QString orientation;
    bool hdrEnabled = false;
};

class MonitorEnumerator final {
public:
    [[nodiscard]] static QVector<MonitorInfo> enumerate();
    [[nodiscard]] static int indexForId(const QVector<MonitorInfo>& monitors, const QString& id);
};

} // namespace LastFrame::Platform
