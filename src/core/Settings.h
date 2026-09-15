#pragma once

#include <filesystem>
#include <string>

#include <nlohmann/json.hpp>

namespace LastFrame::Core {

struct BufferSettings {
    int durationSeconds = 30;
    int ramLimitMiB = 512;
    std::filesystem::path temporaryDirectory;
    bool autoStart = false;
    nlohmann::json extras = nlohmann::json::object();
};

struct CaptureSettings {
    std::string monitorId = "auto";
    std::string source = "full_monitor";
    int regionX = 0;
    int regionY = 0;
    int regionWidth = 0;
    int regionHeight = 0;
    int outputWidth = 0;
    int outputHeight = 0;
    int fps = 60;
    bool showCursor = true;
    bool autoResume = true;
    nlohmann::json extras = nlohmann::json::object();
};

struct VideoSettings {
    std::string container = "mp4";
    std::string codec = "auto";
    std::string preset = "high";
    int customBitrateKbps = 12000;
    int maxFileSizeMiB = 512;
    nlohmann::json extras = nlohmann::json::object();
};

struct AudioSettings {
    bool systemEnabled = true;
    std::string systemDeviceId = "auto";
    bool microphoneEnabled = true;
    std::string microphoneDeviceId = "auto";
    double systemVolume = 1.0;
    double microphoneVolume = 1.0;
    int sampleRate = 48000;
    nlohmann::json extras = nlohmann::json::object();
};

struct HotkeySettings {
    std::string save = "Ctrl+Shift+F10";
    std::string clear = "Ctrl+Shift+F1";
    std::string pause = "Ctrl+Shift+F7";
    std::string toggleCapture = "Ctrl+Shift+F5";
    std::string muteMicrophone;
    nlohmann::json extras = nlohmann::json::object();
};

struct StorageSettings {
    std::filesystem::path clipsDirectory;
    int maxQueueLength = 8;
    nlohmann::json extras = nlohmann::json::object();
};

struct NotificationSettings {
    bool enabled = true;
    bool overlayEnabled = true;
    std::string language = "ru";
    std::string corner = "top_right";
    int durationMs = 3500;
    double opacity = 0.90;
    nlohmann::json extras = nlohmann::json::object();
};

struct PrivacySettings {
    bool updateCheckEnabled = false;
    bool telemetryEnabled = false;
    nlohmann::json extras = nlohmann::json::object();
};

struct Settings {
    int schemaVersion = 1;
    std::string language = "ru";
    BufferSettings buffer;
    CaptureSettings capture;
    VideoSettings video;
    AudioSettings audio;
    HotkeySettings hotkeys;
    StorageSettings storage;
    NotificationSettings notifications;
    PrivacySettings privacy;
    nlohmann::json extras = nlohmann::json::object();

    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static Settings fromJson(const nlohmann::json& json);
    [[nodiscard]] static Settings defaults();
};

} // namespace LastFrame::Core
