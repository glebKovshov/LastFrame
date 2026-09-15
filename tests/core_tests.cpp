#include "core/AppState.h"
#include "core/AudioMixer.h"
#include "core/BoundedQueue.h"
#include "core/FilenameAllocator.h"
#include "core/RateLimiter.h"
#include "core/RingBuffer.h"
#include "core/SettingsStore.h"

#include <cassert>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>
#include <nlohmann/json.hpp>

using namespace LastFrame::Core;

namespace {

std::shared_ptr<const EncodedSegment> segment(std::string id, Timestamp start, Timestamp end) {
    auto value = std::make_shared<EncodedSegment>();
    value->id = std::move(id);
    value->startPts = start;
    value->endPts = end;
    value->startsWithKeyframe = true;
    return value;
}

void testRingBuffer() {
    RingBuffer buffer(3'000'000);
    buffer.append(segment("a", 0, 1'000'000));
    buffer.append(segment("b", 1'000'000, 2'000'000));
    buffer.append(segment("c", 2'000'000, 3'000'000));
    buffer.append(segment("d", 3'000'000, 4'000'000));
    assert(buffer.size() == 3);
    const auto snapshot = buffer.snapshot(4'000'000);
    assert(snapshot.segments.size() == 3);
    assert(snapshot.segments.front()->id == "b");
    assert(snapshot.duration() == 3'000'000);
}

void testRateLimiter() {
    using namespace std::chrono_literals;
    RateLimiter limiter(3, 1s, 5s);
    const auto start = std::chrono::steady_clock::now();
    assert(limiter.tryAccept(start) == RateLimitResult::Accepted);
    assert(limiter.tryAccept(start + 10ms) == RateLimitResult::Accepted);
    assert(limiter.tryAccept(start + 20ms) == RateLimitResult::Accepted);
    assert(limiter.tryAccept(start + 30ms) == RateLimitResult::Cooldown);
    assert(limiter.tryAccept(start + 4s) == RateLimitResult::Cooldown);
    assert(limiter.tryAccept(start + 5s + 31ms) == RateLimitResult::Accepted);
}

AudioBlock pcm(const Timestamp startPts, const int sampleRate, const int channels,
               std::initializer_list<float> samples) {
    AudioBlock block;
    block.startPts = startPts;
    block.sampleRate = sampleRate;
    block.channels = channels;
    block.samples = samples;
    return block;
}

void testAudioMixerSilenceAndClipping() {
    AudioMixer mixer(8'000, 2, 8);
    mixer.setVolumes(1.0, 0.5);
    mixer.pushSystem(pcm(0, 8'000, 1, {0.8F, 0.8F, 0.8F, 0.8F, 0.8F, 0.8F, 0.8F, 0.8F}));
    mixer.pushMicrophone(pcm(0, 8'000, 2, {0.8F, -0.8F, 0.8F, -0.8F, 0.8F, -0.8F, 0.8F, -0.8F,
                                             0.8F, -0.8F, 0.8F, -0.8F, 0.8F, -0.8F, 0.8F, -0.8F}));

    const auto blocks = mixer.drainUntil(1'000);
    assert(blocks.size() == 1);
    assert(blocks.front().frameCount() == 8);
    assert(std::abs(blocks.front().samples[0] - 1.0F) < 0.0001F);
    assert(std::abs(blocks.front().samples[1] - 0.4F) < 0.0001F);

    mixer.setMicrophoneMuted(true);
    mixer.pushSystem(pcm(1'000, 8'000, 1, {0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F, 0.25F}));
    const auto muted = mixer.drainUntil(2'000);
    assert(muted.size() == 1);
    assert(std::abs(muted.front().samples[0] - 0.25F) < 0.0001F);
    assert(std::abs(muted.front().samples[1] - 0.25F) < 0.0001F);
}

void testAudioMixerResampleAndSilenceFill() {
    AudioMixer mixer(16'000, 2, 16);
    mixer.setMicrophoneEnabled(false);
    mixer.pushSystem(pcm(0, 8'000, 1, {0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F, 0.0F, 1.0F}));
    const auto blocks = mixer.drainUntil(1'000);
    assert(blocks.size() == 1);
    assert(blocks.front().samples[0] >= 0.0F && blocks.front().samples[0] <= 1.0F);
    assert(blocks.front().samples[2] >= 0.0F && blocks.front().samples[2] <= 1.0F);

    mixer.reset();
    mixer.pushSystem(pcm(2'000, 16'000, 2, {0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F,
                                            0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F,
                                            0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F,
                                            0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F, 0.5F}));
    const auto padded = mixer.flush();
    assert(padded.size() == 1);
    assert(padded.front().startPts == 2'000);
    assert(std::abs(padded.front().samples[0] - 0.5F) < 0.0001F);
}

void testAudioMixerStartsWithSilence() {
    AudioMixer mixer(8'000, 2, 8);
    mixer.start();
    const auto blocks = mixer.drainUntil(1'000);
    assert(blocks.size() == 1);
    assert(blocks.front().frameCount() == 8);
    for (const float sample : blocks.front().samples) {
        assert(sample == 0.0F);
    }
}

void testStateMachine() {
    StateMachine state;
    assert(state.transition(AppState::Starting));
    assert(state.transition(AppState::Recording));
    assert(state.transition(AppState::Paused));
    assert(!state.transition(AppState::Idle));
    assert(state.transition(AppState::Stopped));
    assert(state.transition(AppState::Starting));
}

void testQueue() {
    BoundedQueue<int> queue(2);
    assert(queue.push(1));
    assert(queue.push(2));
    assert(!queue.push(3));
    assert(queue.rejectedCount() == 1);
    assert(queue.tryPop().value() == 1);
    assert(queue.tryPop().value() == 2);
    assert(!queue.tryPop().has_value());
}

void testFilenameAllocator() {
    const auto directory = std::filesystem::temp_directory_path() / "lastframe-core-tests";
    std::filesystem::create_directories(directory);
    const auto timestamp = std::chrono::system_clock::from_time_t(0);
    const auto first = FilenameAllocator::allocate(directory, timestamp, ".mp4");
    std::ofstream(first).put('x');
    const auto second = FilenameAllocator::allocate(directory, timestamp, "mp4");
    assert(first.filename().string() != second.filename().string());
    std::filesystem::remove(first);
    std::filesystem::remove_all(directory);
}

void testSettings() {
    const auto uniqueName = std::string("lastframe-settings-tests-") +
                            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto path = std::filesystem::temp_directory_path() / uniqueName / "settings.json";
    SettingsStore store(path);

    Settings settings = Settings::defaults();
    settings.buffer.durationSeconds = 45;
    settings.extras["futureFlag"] = true;
    store.save(settings);
    const auto loaded = store.load();
    assert(loaded.buffer.durationSeconds == 45);
    assert(loaded.extras.at("futureFlag") == true);

    const auto normalized = Settings::fromJson({
        {"language", "de"},
        {"buffer", {{"durationSeconds", -4}, {"ramLimitMiB", 999999}}},
        {"capture", {{"source", "unknown"}, {"fps", 1}, {"outputWidth", 99999}}},
        {"video", {{"container", "avi"}, {"codec", "unknown"}, {"preset", "bad"},
                    {"customBitrateKbps", 1}, {"maxFileSizeMiB", 99999}}},
        {"notifications", {{"language", "de"}, {"corner", "center"},
                            {"durationMs", 999999}, {"opacity", -1.0}}},
        {"futureSection", {{"keep", true}}},
    });
    assert(normalized.language == "ru");
    assert(normalized.buffer.durationSeconds == 5);
    assert(normalized.buffer.ramLimitMiB == 16384);
    assert(normalized.capture.source == "full_monitor");
    assert(normalized.capture.fps == 15);
    assert(normalized.capture.outputWidth == 16384);
    assert(normalized.video.container == "mp4");
    assert(normalized.video.codec == "auto");
    assert(normalized.video.preset == "high");
    assert(normalized.video.customBitrateKbps == 1000);
    assert(normalized.video.maxFileSizeMiB == 4096);
    assert(normalized.notifications.language == "ru");
    assert(normalized.notifications.corner == "top_right");
    assert(normalized.notifications.durationMs == 10000);
    assert(normalized.notifications.opacity == 0.20);
    const auto webmPair = Settings::fromJson({{"video", {{"container", "webm"}, {"codec", "h264_nvenc"}}}});
    assert(webmPair.video.codec == "libvpx-vp9");
    const auto mp4Pair = Settings::fromJson({{"video", {{"container", "mp4"}, {"codec", "libvpx-vp9"}}}});
    assert(mp4Pair.video.codec == "auto");
    for (const char* codec : {"h264_nvenc", "h264_amf", "h264_qsv", "h264_videotoolbox", "libx264"}) {
        const auto codecSettings = Settings::fromJson({{"video", {{"codec", codec}}}});
        assert(codecSettings.video.codec == codec);
    }
    const auto vp9Settings = Settings::fromJson({{"video", {{"container", "webm"}, {"codec", "libvpx-vp9"}}}});
    assert(vp9Settings.video.codec == "libvpx-vp9");
    assert(normalized.extras.at("futureSection").at("keep") == true);

    {
        std::ofstream broken(path, std::ios::trunc);
        broken << "{broken";
    }
    bool recovered = false;
    const auto recoveredSettings = store.load(&recovered);
    assert(recovered);
    assert(recoveredSettings.buffer.durationSeconds == 30);
    bool backupFound = false;
    for (const auto& entry : std::filesystem::directory_iterator(path.parent_path())) {
        if (entry.path().filename().string().starts_with("settings.json.broken-")) {
            backupFound = true;
            std::filesystem::remove(entry.path());
        }
    }
    assert(backupFound);
    std::filesystem::remove_all(path.parent_path());
}

} // namespace

int main() {
    testRingBuffer();
    testRateLimiter();
    testAudioMixerSilenceAndClipping();
    testAudioMixerResampleAndSilenceFill();
    testAudioMixerStartsWithSilence();
    testStateMachine();
    testQueue();
    testFilenameAllocator();
    testSettings();
    std::cout << "LastFrame core tests passed\n";
}
