#pragma once

#include <QString>
#include <QVector>

namespace LastFrame::Platform {

struct AudioDeviceInfo {
    QString id;
    QString name;
};

class AudioDeviceEnumerator final {
public:
    [[nodiscard]] static QVector<AudioDeviceInfo> microphones(const QString& ffmpegPath);
    [[nodiscard]] static QVector<AudioDeviceInfo> systemOutputs(const QString& ffmpegPath);
};

} // namespace LastFrame::Platform
