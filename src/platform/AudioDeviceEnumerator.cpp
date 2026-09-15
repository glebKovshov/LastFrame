#include "platform/AudioDeviceEnumerator.h"

#include <QProcess>
#include <QRegularExpression>

namespace LastFrame::Platform {
namespace {

QVector<AudioDeviceInfo> enumerate(const QString& ffmpegPath, const QString& format) {
    if (ffmpegPath.isEmpty()) {
        return {};
    }
    QProcess probe;
    probe.setProcessChannelMode(QProcess::MergedChannels);
    probe.start(ffmpegPath, {QStringLiteral("-hide_banner"), QStringLiteral("-f"), format,
                             QStringLiteral("-list_devices"), QStringLiteral("true"), QStringLiteral("-i"),
                             QStringLiteral("dummy")});
    if (!probe.waitForFinished(3000)) {
        probe.kill();
        return {};
    }

    const QString output = QString::fromUtf8(probe.readAll());
    const QRegularExpression friendlyExpression(QStringLiteral("\\\"([^\\\"]+)\\\"\\s+\\(audio\\)"));
    const QRegularExpression alternativeExpression(QStringLiteral("Alternative name \\\"([^\\\"]+)\\\""));
    QVector<AudioDeviceInfo> result;
    QString pendingName;
    for (const QString& line : output.split(QRegularExpression(QStringLiteral("[\\r\\n]+")), Qt::SkipEmptyParts)) {
        const auto friendlyMatch = friendlyExpression.match(line);
        if (friendlyMatch.hasMatch()) {
            pendingName = friendlyMatch.captured(1);
            continue;
        }
        const auto alternativeMatch = alternativeExpression.match(line);
        if (alternativeMatch.hasMatch() && !pendingName.isEmpty()) {
            result.push_back({alternativeMatch.captured(1), pendingName});
            pendingName.clear();
        }
    }
    if (!pendingName.isEmpty()) {
        result.push_back({pendingName, pendingName});
    }
    return result;
}

} // namespace

QVector<AudioDeviceInfo> AudioDeviceEnumerator::microphones(const QString& ffmpegPath) {
#if defined(Q_OS_WIN)
    return enumerate(ffmpegPath, QStringLiteral("dshow"));
#else
    Q_UNUSED(ffmpegPath)
    return {};
#endif
}

QVector<AudioDeviceInfo> AudioDeviceEnumerator::systemOutputs(const QString& ffmpegPath) {
#if defined(Q_OS_WIN)
    return enumerate(ffmpegPath, QStringLiteral("wasapi"));
#else
    Q_UNUSED(ffmpegPath)
    return {};
#endif
}

} // namespace LastFrame::Platform
