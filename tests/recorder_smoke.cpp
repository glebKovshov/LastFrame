#include "core/Settings.h"
#include "media/PortableSegmentRecorder.h"

#include <QGuiApplication>
#include <QCoreApplication>
#include <QFileInfo>
#include <QTimer>

#include <filesystem>
#include <iostream>

int main(int argc, char* argv[]) {
    QGuiApplication application(argc, argv);
    LastFrame::Core::Settings settings = LastFrame::Core::Settings::defaults();
    const auto root = std::filesystem::temp_directory_path() /
                      ("lastframe-recorder-smoke-" + std::to_string(QCoreApplication::applicationPid()));
    settings.buffer.durationSeconds = 5;
    settings.buffer.temporaryDirectory = root / "segments";
    settings.storage.clipsDirectory = root / "clips";
    // Exercise the default audio path as well. On FFmpeg builds without a
    // WASAPI demuxer the recorder must fall back to video-only capture.
    settings.audio.systemEnabled = true;
    settings.capture.fps = 30;

    LastFrame::Media::PortableSegmentRecorder recorder;
    bool success = false;
    QObject::connect(&recorder, &LastFrame::Media::PortableSegmentRecorder::started,
                     &application, [&recorder] {
                         QTimer::singleShot(4500, &recorder, &LastFrame::Media::PortableSegmentRecorder::saveClip);
                     });
    QObject::connect(&recorder, &LastFrame::Media::PortableSegmentRecorder::clipSaved,
                     &application, [&application, &success](const QString& path) {
                         success = QFileInfo::exists(path) && QFileInfo(path).size() > 0;
                         QTimer::singleShot(250, &application, &QCoreApplication::quit);
                     });
    QObject::connect(&recorder, &LastFrame::Media::PortableSegmentRecorder::error,
                     &application, [](const QString& text) {
                        std::cerr << text.toStdString() << '\n';
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
