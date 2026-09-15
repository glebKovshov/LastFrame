#include "core/Settings.h"
#include "media/PortableSegmentRecorder.h"

#include <QGuiApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTimer>

#include <filesystem>
#include <iostream>
#include <set>

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    LastFrame::Core::Settings settings = LastFrame::Core::Settings::defaults();
    const auto root = std::filesystem::temp_directory_path() /
                      ("lastframe-recorder-smoke-" + std::to_string(QCoreApplication::applicationPid()));
    settings.buffer.durationSeconds = 5;
    settings.buffer.temporaryDirectory = root / "segments";
    settings.storage.clipsDirectory = root / "clips";
    // Exercise the audio fallback path as well. On FFmpeg builds without a
    // WASAPI demuxer the recorder should continue with the microphone when
    // available, then fall back to video-only capture.
    settings.audio.systemEnabled = true;
    settings.audio.microphoneEnabled = qEnvironmentVariable("LASTFRAME_SMOKE_MICROPHONE", "1") != "0";
    // Exercise the region path on the real desktop instead of only testing
    // the full-monitor default.
    settings.capture.source = "custom_region";
    settings.capture.regionX = 0;
    settings.capture.regionY = 0;
    settings.capture.regionWidth = 640;
    settings.capture.regionHeight = 360;
    settings.capture.outputWidth = 640;
    settings.capture.outputHeight = 360;
    settings.capture.fps = 30;
    const QString requestedContainer = qEnvironmentVariable("LASTFRAME_SMOKE_CONTAINER");
    if (!requestedContainer.isEmpty()) {
        settings.video.container = requestedContainer.toStdString();
    }

    LastFrame::Media::PortableSegmentRecorder recorder;
    bool success = false;
    std::set<QString> savedPaths;
    QObject::connect(&recorder, &LastFrame::Media::PortableSegmentRecorder::started,
                     &application, [&recorder] {
                         QTimer::singleShot(4500, &recorder, &LastFrame::Media::PortableSegmentRecorder::saveClip);
                         QTimer::singleShot(4700, &recorder, &LastFrame::Media::PortableSegmentRecorder::saveClip);
                     });
    QObject::connect(&recorder, &LastFrame::Media::PortableSegmentRecorder::clipSaved,
                     &application, [&application, &success, &savedPaths](const QString& path) {
                         if (QFileInfo::exists(path) && QFileInfo(path).size() > 0) {
                             savedPaths.insert(path);
                         }
                         if (savedPaths.size() == 2) {
                             success = true;
                             QTimer::singleShot(250, &application, &QCoreApplication::quit);
                         }
                     });
    QObject::connect(&recorder, &LastFrame::Media::PortableSegmentRecorder::error,
                     &application, [](const QString& text) {
                        std::cerr << text.toStdString() << '\n';
                    });
    QObject::connect(&recorder, &LastFrame::Media::PortableSegmentRecorder::message,
                     &application, [](const QString& text) {
                         std::cerr << "message: " << text.toStdString() << '\n';
                     });

    recorder.start(settings);
    QTimer::singleShot(15000, &application, &QCoreApplication::quit);
    application.exec();
    recorder.stop();
    if (!success) {
        std::cerr << "Recorder smoke test did not produce a clip\n";
        return 1;
    }
    std::cout << "Recorder smoke test passed\n";
    return 0;
}
