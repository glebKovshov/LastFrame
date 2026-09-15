#include "core/Settings.h"

#include <algorithm>
#include <initializer_list>

namespace LastFrame::Core {
namespace {

template <typename T>
T readValue(const nlohmann::json& object, const char* key, const T& fallback) {
    if (!object.is_object() || !object.contains(key)) {
        return fallback;
    }
    try {
        return object.at(key).get<T>();
    } catch (...) {
        return fallback;
    }
}

std::filesystem::path pathFromJson(const nlohmann::json& object, const char* key) {
    return std::filesystem::path(readValue<std::string>(object, key, {}));
}

nlohmann::json sectionExtras(const nlohmann::json& object,
                             std::initializer_list<const char*> knownKeys) {
    nlohmann::json extras = nlohmann::json::object();
    if (!object.is_object()) {
        return extras;
    }
    for (const auto& [key, value] : object.items()) {
        if (std::find(knownKeys.begin(), knownKeys.end(), key) == knownKeys.end()) {
            extras[key] = value;
        }
    }
    return extras;
}

} // namespace

Settings Settings::defaults() {
    Settings settings;
    settings.storage.clipsDirectory = std::filesystem::path();
    return settings;
}

nlohmann::json Settings::toJson() const {
    nlohmann::json result = extras.is_object() ? extras : nlohmann::json::object();
    result["schemaVersion"] = schemaVersion;
    result["language"] = language;

    result["buffer"] = buffer.extras;
    result["buffer"].update({
        {"durationSeconds", buffer.durationSeconds},
        {"ramLimitMiB", buffer.ramLimitMiB},
        {"temporaryDirectory", buffer.temporaryDirectory.generic_string()},
        {"autoStart", buffer.autoStart},
    });

    result["capture"] = capture.extras;
    result["capture"].update({
        {"monitorId", capture.monitorId},
        {"source", capture.source},
        {"region", {{"x", capture.regionX}, {"y", capture.regionY},
                     {"width", capture.regionWidth}, {"height", capture.regionHeight}}},
        {"outputWidth", capture.outputWidth},
        {"outputHeight", capture.outputHeight},
        {"fps", capture.fps},
        {"showCursor", capture.showCursor},
        {"autoResume", capture.autoResume},
    });

    result["video"] = video.extras;
    result["video"].update({
        {"container", video.container},
        {"codec", video.codec},
        {"preset", video.preset},
        {"customBitrateKbps", video.customBitrateKbps},
        {"maxFileSizeMiB", video.maxFileSizeMiB},
    });

    result["audio"] = audio.extras;
    result["audio"].update({
        {"systemEnabled", audio.systemEnabled},
        {"systemDeviceId", audio.systemDeviceId},
        {"microphoneEnabled", audio.microphoneEnabled},
        {"microphoneDeviceId", audio.microphoneDeviceId},
        {"systemVolume", audio.systemVolume},
        {"microphoneVolume", audio.microphoneVolume},
        {"sampleRate", audio.sampleRate},
    });

    result["hotkeys"] = hotkeys.extras;
    result["hotkeys"].update({
        {"save", hotkeys.save}, {"clear", hotkeys.clear}, {"pause", hotkeys.pause},
        {"toggleCapture", hotkeys.toggleCapture}, {"muteMicrophone", hotkeys.muteMicrophone},
    });

    result["storage"] = storage.extras;
    result["storage"].update({
        {"clipsDirectory", storage.clipsDirectory.generic_string()},
        {"maxQueueLength", storage.maxQueueLength},
    });

    result["notifications"] = notifications.extras;
    result["notifications"].update({
        {"enabled", notifications.enabled}, {"overlayEnabled", notifications.overlayEnabled},
        {"language", notifications.language}, {"corner", notifications.corner},
        {"durationMs", notifications.durationMs}, {"opacity", notifications.opacity},
    });

    result["privacy"] = privacy.extras;
    result["privacy"].update({
        {"updateCheckEnabled", privacy.updateCheckEnabled},
        {"telemetryEnabled", privacy.telemetryEnabled},
    });
    return result;
}

Settings Settings::fromJson(const nlohmann::json& json) {
    Settings settings = defaults();
    if (!json.is_object()) {
        return settings;
    }

    settings.schemaVersion = std::max(1, readValue(json, "schemaVersion", 1));
    settings.language = readValue(json, "language", settings.language);
    if (settings.language != "ru" && settings.language != "en") {
        settings.language = "ru";
    }
    settings.extras = sectionExtras(json, {"schemaVersion", "language", "buffer", "capture", "video",
                                           "audio", "hotkeys", "storage", "notifications", "privacy"});

    const auto buffer = json.value("buffer", nlohmann::json::object());
    settings.buffer.durationSeconds = std::clamp(readValue(buffer, "durationSeconds", settings.buffer.durationSeconds), 5, 300);
    settings.buffer.ramLimitMiB = std::clamp(readValue(buffer, "ramLimitMiB", settings.buffer.ramLimitMiB), 64, 16384);
    settings.buffer.temporaryDirectory = pathFromJson(buffer, "temporaryDirectory");
    settings.buffer.autoStart = readValue(buffer, "autoStart", settings.buffer.autoStart);
    settings.buffer.extras = sectionExtras(buffer, {"durationSeconds", "ramLimitMiB", "temporaryDirectory", "autoStart"});

    const auto capture = json.value("capture", nlohmann::json::object());
    settings.capture.monitorId = readValue(capture, "monitorId", settings.capture.monitorId);
    settings.capture.source = readValue(capture, "source", settings.capture.source);
    if (settings.capture.source != "full_monitor" && settings.capture.source != "custom_region") {
        settings.capture.source = "full_monitor";
    }
    const auto region = capture.value("region", nlohmann::json::object());
    settings.capture.regionX = std::clamp(readValue(region, "x", settings.capture.regionX), -16384, 16384);
    settings.capture.regionY = std::clamp(readValue(region, "y", settings.capture.regionY), -16384, 16384);
    settings.capture.regionWidth = std::clamp(readValue(region, "width", settings.capture.regionWidth), 0, 16384);
    settings.capture.regionHeight = std::clamp(readValue(region, "height", settings.capture.regionHeight), 0, 16384);
    settings.capture.outputWidth = std::clamp(readValue(capture, "outputWidth", settings.capture.outputWidth), 0, 16384);
    settings.capture.outputHeight = std::clamp(readValue(capture, "outputHeight", settings.capture.outputHeight), 0, 16384);
    settings.capture.fps = std::clamp(readValue(capture, "fps", settings.capture.fps), 15, 360);
    settings.capture.showCursor = readValue(capture, "showCursor", settings.capture.showCursor);
    settings.capture.autoResume = readValue(capture, "autoResume", settings.capture.autoResume);
    settings.capture.extras = sectionExtras(capture, {"monitorId", "source", "region", "outputWidth", "outputHeight",
                                                      "fps", "showCursor", "autoResume"});

    const auto video = json.value("video", nlohmann::json::object());
    settings.video.container = readValue(video, "container", settings.video.container);
    if (settings.video.container != "mp4" && settings.video.container != "mkv" && settings.video.container != "webm") {
        settings.video.container = "mp4";
    }
    settings.video.codec = readValue(video, "codec", settings.video.codec);
    if (settings.video.codec != "auto" && settings.video.codec != "h264_nvenc" &&
        settings.video.codec != "h264_amf" && settings.video.codec != "h264_qsv" &&
        settings.video.codec != "h264_videotoolbox" && settings.video.codec != "libx264" &&
        settings.video.codec != "libvpx-vp9") {
        settings.video.codec = "auto";
    }
    // VP9 is the WebM profile in the portable recorder. Keep invalid
    // container/codec pairs from entering the media layer through JSON or
    // another non-UI caller.
    if (settings.video.container == "webm" && settings.video.codec != "libvpx-vp9") {
        settings.video.codec = "libvpx-vp9";
    } else if (settings.video.container != "webm" && settings.video.codec == "libvpx-vp9") {
        settings.video.codec = "auto";
    }
    settings.video.preset = readValue(video, "preset", settings.video.preset);
    if (settings.video.preset != "low" && settings.video.preset != "medium" && settings.video.preset != "high" &&
        settings.video.preset != "ultra" && settings.video.preset != "custom") {
        settings.video.preset = "high";
    }
    settings.video.customBitrateKbps = std::clamp(readValue(video, "customBitrateKbps", settings.video.customBitrateKbps), 1000, 100000);
    settings.video.maxFileSizeMiB = std::clamp(readValue(video, "maxFileSizeMiB", settings.video.maxFileSizeMiB), 64, 4096);
    settings.video.extras = sectionExtras(video, {"container", "codec", "preset", "customBitrateKbps", "maxFileSizeMiB"});

    const auto audio = json.value("audio", nlohmann::json::object());
    settings.audio.systemEnabled = readValue(audio, "systemEnabled", settings.audio.systemEnabled);
    settings.audio.systemDeviceId = readValue(audio, "systemDeviceId", settings.audio.systemDeviceId);
    settings.audio.microphoneEnabled = readValue(audio, "microphoneEnabled", settings.audio.microphoneEnabled);
    settings.audio.microphoneDeviceId = readValue(audio, "microphoneDeviceId", settings.audio.microphoneDeviceId);
    settings.audio.systemVolume = std::clamp(readValue(audio, "systemVolume", settings.audio.systemVolume), 0.0, 2.0);
    settings.audio.microphoneVolume = std::clamp(readValue(audio, "microphoneVolume", settings.audio.microphoneVolume), 0.0, 2.0);
    settings.audio.sampleRate = std::clamp(readValue(audio, "sampleRate", settings.audio.sampleRate), 8000, 192000);
    settings.audio.extras = sectionExtras(audio, {"systemEnabled", "systemDeviceId", "microphoneEnabled", "microphoneDeviceId",
                                                  "systemVolume", "microphoneVolume", "sampleRate"});

    const auto hotkeys = json.value("hotkeys", nlohmann::json::object());
    settings.hotkeys.save = readValue(hotkeys, "save", settings.hotkeys.save);
    settings.hotkeys.clear = readValue(hotkeys, "clear", settings.hotkeys.clear);
    settings.hotkeys.pause = readValue(hotkeys, "pause", settings.hotkeys.pause);
    settings.hotkeys.toggleCapture = readValue(hotkeys, "toggleCapture", settings.hotkeys.toggleCapture);
    settings.hotkeys.muteMicrophone = readValue(hotkeys, "muteMicrophone", settings.hotkeys.muteMicrophone);
    settings.hotkeys.extras = sectionExtras(hotkeys, {"save", "clear", "pause", "toggleCapture", "muteMicrophone"});

    const auto storage = json.value("storage", nlohmann::json::object());
    settings.storage.clipsDirectory = pathFromJson(storage, "clipsDirectory");
    settings.storage.maxQueueLength = std::clamp(readValue(storage, "maxQueueLength", settings.storage.maxQueueLength), 1, 32);
    settings.storage.extras = sectionExtras(storage, {"clipsDirectory", "maxQueueLength"});

    const auto notifications = json.value("notifications", nlohmann::json::object());
    settings.notifications.enabled = readValue(notifications, "enabled", settings.notifications.enabled);
    settings.notifications.overlayEnabled = readValue(notifications, "overlayEnabled", settings.notifications.overlayEnabled);
    settings.notifications.language = readValue(notifications, "language", settings.notifications.language);
    if (settings.notifications.language != "ru" && settings.notifications.language != "en") {
        settings.notifications.language = "ru";
    }
    settings.notifications.corner = readValue(notifications, "corner", settings.notifications.corner);
    if (settings.notifications.corner != "top_left" && settings.notifications.corner != "top_right" &&
        settings.notifications.corner != "bottom_left" && settings.notifications.corner != "bottom_right") {
        settings.notifications.corner = "top_right";
    }
    settings.notifications.durationMs = std::clamp(
        readValue(notifications, "durationMs", settings.notifications.durationMs), 500, 10000);
    settings.notifications.opacity = std::clamp(
        readValue(notifications, "opacity", settings.notifications.opacity), 0.20, 1.0);
    settings.notifications.extras = sectionExtras(
        notifications, {"enabled", "overlayEnabled", "language", "corner", "durationMs", "opacity"});

    const auto privacy = json.value("privacy", nlohmann::json::object());
    settings.privacy.updateCheckEnabled = readValue(privacy, "updateCheckEnabled", settings.privacy.updateCheckEnabled);
    settings.privacy.telemetryEnabled = readValue(privacy, "telemetryEnabled", settings.privacy.telemetryEnabled);
    settings.privacy.extras = sectionExtras(privacy, {"updateCheckEnabled", "telemetryEnabled"});
    return settings;
}

} // namespace LastFrame::Core
