#include "platform/Diagnostics.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>
#include <QSysInfo>
#include <QTextStream>

namespace LastFrame::Platform {

QString Diagnostics::logPath() {
    const QString directory = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
    if (directory.isEmpty()) {
        return {};
    }
    QDir().mkpath(directory);
    return QDir(directory).filePath(QStringLiteral("lastframe.log"));
}

void Diagnostics::append(const QString& level, const QString& message) {
    const QString path = logPath();
    if (path.isEmpty()) {
        return;
    }
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return;
    }
    QTextStream stream(&file);
    stream << QDateTime::currentDateTime().toString(Qt::ISODateWithMs) << " [" << level << "] "
           << message.left(1000).replace(QLatin1Char('\n'), QLatin1Char(' ')) << Qt::endl;
}

QString Diagnostics::snapshot(const QString& ffmpegPath, const int monitorCount,
                              const QString& selectedMonitorId, const bool recording, const bool paused) {
    QString result;
    QTextStream stream(&result);
    stream << "LastFrame diagnostics\n"
           << "appVersion=" << QCoreApplication::applicationVersion() << "\n"
           << "qtVersion=" << QT_VERSION_STR << "\n"
           << "os=" << QSysInfo::prettyProductName() << "\n"
           << "cpuArchitecture=" << QSysInfo::currentCpuArchitecture() << "\n"
           << "kernelType=" << QSysInfo::kernelType() << "\n"
           << "kernelVersion=" << QSysInfo::kernelVersion() << "\n"
           << "monitorCount=" << monitorCount << "\n"
           << "selectedMonitorId=" << selectedMonitorId << "\n"
           << "recording=" << (recording ? "true" : "false") << "\n"
           << "paused=" << (paused ? "true" : "false") << "\n"
           << "ffmpegAvailable=" << (!ffmpegPath.isEmpty() ? "true" : "false") << "\n"
           << "ffmpegPath=" << QDir::toNativeSeparators(ffmpegPath) << "\n"
           << "ffmpegName=" << (ffmpegPath.isEmpty() ? "" : QFileInfo(ffmpegPath).fileName()) << "\n"
           << "logPath=" << logPath() << "\n";
    return result;
}

} // namespace LastFrame::Platform
