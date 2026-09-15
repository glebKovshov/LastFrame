#pragma once

#include "core/MediaTypes.h"

#include <cstddef>
#include <cstdint>
#include <deque>
#include <optional>
#include <vector>

namespace LastFrame::Core {

// Interleaved normalized PCM. Timestamps are expressed in the same microsecond
// timebase as video PTS values, so audio can be exported against one clock.
struct AudioBlock final {
    Timestamp startPts = 0;
    int sampleRate = 48'000;
    int channels = 2;
    std::vector<float> samples;

    [[nodiscard]] std::size_t frameCount() const noexcept {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }

    [[nodiscard]] Timestamp endPts() const noexcept {
        if (sampleRate <= 0) {
            return startPts;
        }
        return startPts + static_cast<Timestamp>(
                              (static_cast<std::int64_t>(frameCount()) * 1'000'000) / sampleRate);
    }

    [[nodiscard]] bool isValid() const noexcept {
        return startPts >= 0 && sampleRate > 0 && channels > 0 &&
               samples.size() % static_cast<std::size_t>(channels) == 0 && !samples.empty();
    }
};

struct MixedAudioBlock final {
    Timestamp startPts = 0;
    int sampleRate = 48'000;
    int channels = 2;
    std::vector<float> samples;

    [[nodiscard]] std::size_t frameCount() const noexcept {
        return channels > 0 ? samples.size() / static_cast<std::size_t>(channels) : 0;
    }

    [[nodiscard]] Timestamp endPts() const noexcept {
        if (sampleRate <= 0) {
            return startPts;
        }
        return startPts + static_cast<Timestamp>(
                              (static_cast<std::int64_t>(frameCount()) * 1'000'000) / sampleRate);
    }
};

class AudioMixer final {
public:
    explicit AudioMixer(int sampleRate = 48'000, int channels = 2, int blockFrames = 480);

    void setVolumes(double systemVolume, double microphoneVolume) noexcept;
    void setSystemEnabled(bool enabled) noexcept { systemEnabled_ = enabled; }
    void setMicrophoneEnabled(bool enabled) noexcept { microphoneEnabled_ = enabled; }
    void setMicrophoneMuted(bool muted) noexcept { microphoneMuted_ = muted; }

    void pushSystem(AudioBlock block);
    void pushMicrophone(AudioBlock block);

    // Emits complete fixed-size blocks up to the supplied master clock. Missing
    // source samples are represented by silence instead of delaying the clock.
    [[nodiscard]] std::vector<MixedAudioBlock> drainUntil(Timestamp masterNowPts);

    // Emits all remaining source data, padding the final block with silence when
    // necessary. This is used during an orderly stop/export flush.
    [[nodiscard]] std::vector<MixedAudioBlock> flush();

    void reset() noexcept;

    [[nodiscard]] int sampleRate() const noexcept { return sampleRate_; }
    [[nodiscard]] int channels() const noexcept { return channels_; }
    [[nodiscard]] int blockFrames() const noexcept { return blockFrames_; }
    [[nodiscard]] std::optional<Timestamp> masterClock() const noexcept { return nextOutputPts_; }
    [[nodiscard]] std::size_t queuedFrames() const noexcept;

private:
    struct SourceQueue final {
        std::deque<AudioBlock> blocks;
    };

    [[nodiscard]] std::vector<MixedAudioBlock> drainRange(Timestamp endPts, bool allowPartial);
    [[nodiscard]] MixedAudioBlock renderBlock(Timestamp startPts, int frames) const;
    [[nodiscard]] float sampleAt(const SourceQueue& source, Timestamp pts, int outputChannel) const noexcept;
    void push(SourceQueue& source, AudioBlock block);
    void discardConsumed(SourceQueue& source, Timestamp beforePts);
    [[nodiscard]] static double clampVolume(double volume) noexcept;

    int sampleRate_;
    int channels_;
    int blockFrames_;
    double systemVolume_ = 1.0;
    double microphoneVolume_ = 1.0;
    bool systemEnabled_ = true;
    bool microphoneEnabled_ = true;
    bool microphoneMuted_ = false;
    SourceQueue system_;
    SourceQueue microphone_;
    std::optional<Timestamp> nextOutputPts_;
    bool clockStarted_ = false;
};

} // namespace LastFrame::Core
