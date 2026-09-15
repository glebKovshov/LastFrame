#include "core/AudioMixer.h"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace LastFrame::Core {

namespace {

[[nodiscard]] Timestamp durationForFrames(const int frames, const int sampleRate) noexcept {
    return static_cast<Timestamp>((static_cast<std::int64_t>(frames) * 1'000'000) / sampleRate);
}

[[nodiscard]] float clampSample(const double value) noexcept {
    return static_cast<float>(std::clamp(value, -1.0, 1.0));
}

} // namespace

AudioMixer::AudioMixer(const int sampleRate, const int channels, const int blockFrames)
    : sampleRate_(sampleRate), channels_(channels), blockFrames_(blockFrames) {
    if (sampleRate < 8'000 || sampleRate > 192'000 || channels < 1 || channels > 8 || blockFrames <= 0) {
        throw std::invalid_argument("AudioMixer format is outside the supported range");
    }
}

void AudioMixer::setVolumes(const double systemVolume, const double microphoneVolume) noexcept {
    systemVolume_ = clampVolume(systemVolume);
    microphoneVolume_ = clampVolume(microphoneVolume);
}

void AudioMixer::start(const Timestamp masterStartPts) noexcept {
    if (masterStartPts >= 0 && !nextOutputPts_.has_value()) {
        nextOutputPts_ = masterStartPts;
    }
}

void AudioMixer::pushSystem(AudioBlock block) {
    push(system_, std::move(block));
}

void AudioMixer::pushMicrophone(AudioBlock block) {
    push(microphone_, std::move(block));
}

void AudioMixer::push(SourceQueue& source, AudioBlock block) {
    if (!block.isValid() || block.sampleRate < 8'000 || block.sampleRate > 192'000) {
        throw std::invalid_argument("AudioMixer accepts only valid PCM blocks");
    }
    if (!source.blocks.empty() && block.startPts < source.blocks.back().startPts) {
        throw std::invalid_argument("AudioMixer source blocks must be timestamp ordered");
    }
    if (!nextOutputPts_.has_value()) {
        nextOutputPts_ = block.startPts;
    } else if (!clockStarted_) {
        nextOutputPts_ = std::min(*nextOutputPts_, block.startPts);
    }
    source.blocks.push_back(std::move(block));
}

std::vector<MixedAudioBlock> AudioMixer::drainUntil(const Timestamp masterNowPts) {
    if (masterNowPts <= 0 || !nextOutputPts_.has_value()) {
        return {};
    }
    return drainRange(masterNowPts, false);
}

std::vector<MixedAudioBlock> AudioMixer::flush() {
    if (!nextOutputPts_.has_value()) {
        return {};
    }

    Timestamp sourceEnd = *nextOutputPts_;
    for (const SourceQueue* source : {&system_, &microphone_}) {
        if (!source->blocks.empty()) {
            sourceEnd = std::max(sourceEnd, source->blocks.back().endPts());
        }
    }
    return drainRange(sourceEnd, true);
}

std::vector<MixedAudioBlock> AudioMixer::drainRange(const Timestamp endPts, const bool allowPartial) {
    std::vector<MixedAudioBlock> output;
    if (!nextOutputPts_.has_value() || endPts <= *nextOutputPts_) {
        return output;
    }

    clockStarted_ = true;

    const Timestamp fullBlockDuration = durationForFrames(blockFrames_, sampleRate_);
    while (*nextOutputPts_ + fullBlockDuration <= endPts) {
        output.push_back(renderBlock(*nextOutputPts_, blockFrames_));
        *nextOutputPts_ += fullBlockDuration;
        discardConsumed(system_, *nextOutputPts_);
        discardConsumed(microphone_, *nextOutputPts_);
    }

    if (allowPartial && *nextOutputPts_ < endPts) {
        const auto remaining = endPts - *nextOutputPts_;
        const int frames = std::max(1, std::min(
            blockFrames_, static_cast<int>((remaining * sampleRate_ + 999'999) / 1'000'000)));
        output.push_back(renderBlock(*nextOutputPts_, frames));
        *nextOutputPts_ += durationForFrames(frames, sampleRate_);
        discardConsumed(system_, *nextOutputPts_);
        discardConsumed(microphone_, *nextOutputPts_);
    }
    return output;
}

MixedAudioBlock AudioMixer::renderBlock(const Timestamp startPts, const int frames) const {
    MixedAudioBlock output;
    output.startPts = startPts;
    output.sampleRate = sampleRate_;
    output.channels = channels_;
    output.samples.resize(static_cast<std::size_t>(frames) * static_cast<std::size_t>(channels_), 0.0F);

    for (int frame = 0; frame < frames; ++frame) {
        const Timestamp pts = startPts + durationForFrames(frame, sampleRate_);
        for (int channel = 0; channel < channels_; ++channel) {
            double mixed = 0.0;
            if (systemEnabled_) {
                mixed += static_cast<double>(sampleAt(system_, pts, channel)) * systemVolume_;
            }
            if (microphoneEnabled_ && !microphoneMuted_) {
                mixed += static_cast<double>(sampleAt(microphone_, pts, channel)) * microphoneVolume_;
            }
            output.samples[static_cast<std::size_t>(frame) * static_cast<std::size_t>(channels_) +
                          static_cast<std::size_t>(channel)] = clampSample(mixed);
        }
    }
    return output;
}

float AudioMixer::sampleAt(const SourceQueue& source, const Timestamp pts, const int outputChannel) const noexcept {
    for (const AudioBlock& block : source.blocks) {
        if (pts < block.startPts) {
            break;
        }
        if (pts >= block.endPts()) {
            continue;
        }

        const double position = static_cast<double>(pts - block.startPts) * block.sampleRate / 1'000'000.0;
        const auto firstFrame = static_cast<std::size_t>(std::max(0.0, std::floor(position)));
        const double fraction = position - static_cast<double>(firstFrame);
        const std::size_t frameCount = block.frameCount();
        if (frameCount == 0 || firstFrame >= frameCount) {
            return 0.0F;
        }

        const int sourceChannel = block.channels == 1 ? 0 : std::min(outputChannel, block.channels - 1);
        const auto at = [&](const std::size_t frame) {
            return block.samples[frame * static_cast<std::size_t>(block.channels) +
                                 static_cast<std::size_t>(sourceChannel)];
        };
        const float first = at(firstFrame);
        const float second = firstFrame + 1 < frameCount ? at(firstFrame + 1) : first;
        return static_cast<float>(first + (second - first) * fraction);
    }
    return 0.0F;
}

void AudioMixer::discardConsumed(SourceQueue& source, const Timestamp beforePts) {
    while (!source.blocks.empty() && source.blocks.front().endPts() <= beforePts) {
        source.blocks.pop_front();
    }
}

void AudioMixer::reset() noexcept {
    system_.blocks.clear();
    microphone_.blocks.clear();
    nextOutputPts_.reset();
    clockStarted_ = false;
}

std::size_t AudioMixer::queuedFrames() const noexcept {
    std::size_t frames = 0;
    for (const AudioBlock& block : system_.blocks) {
        frames += block.frameCount();
    }
    for (const AudioBlock& block : microphone_.blocks) {
        frames += block.frameCount();
    }
    return frames;
}

double AudioMixer::clampVolume(const double volume) noexcept {
    if (!std::isfinite(volume)) {
        return 1.0;
    }
    return std::clamp(volume, 0.0, 2.0);
}

} // namespace LastFrame::Core
