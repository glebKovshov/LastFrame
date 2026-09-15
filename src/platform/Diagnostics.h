#pragma once

#include <QString>

namespace LastFrame::Platform {

class Diagnostics final {
public:
    [[nodiscard]] static QString logPath();
    [[nodiscard]] static QString snapshot(const QString& ffmpegPath, int monitorCount,
                                          const QString& selectedMonitorId, bool recording, bool paused);
    static void append(const QString& level, const QString& message);
};

} // namespace LastFrame::Platform
