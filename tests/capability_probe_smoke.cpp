#include "platform/FfmpegCapabilityProbe.h"

#include <QCoreApplication>
#include <QTimer>

#include <iostream>

int main(int argc, char* argv[]) {
    QCoreApplication application(argc, argv);
    const QString ffmpegPath = argc > 1 ? QString::fromLocal8Bit(argv[1]) : QStringLiteral("ffmpeg");
    LastFrame::Platform::FfmpegCapabilities result;
    bool completed = false;
    LastFrame::Platform::FfmpegCapabilityProbe probe;
    QObject::connect(&probe, &LastFrame::Platform::FfmpegCapabilityProbe::finished, &application,
                     [&application, &result, &completed](const LastFrame::Platform::FfmpegCapabilities& capabilities) {
                         result = capabilities;
                         completed = true;
                         application.quit();
                     });
    QTimer::singleShot(20000, &application, &QCoreApplication::quit);
    probe.probe(ffmpegPath);
    application.exec();

    if (!completed || !result.available) {
        std::cerr << "FFmpeg capability probe did not complete\n";
        return 1;
    }
    std::cout << "FFmpeg capability probe passed: " << result.version.toStdString() << '\n';
    return 0;
}
