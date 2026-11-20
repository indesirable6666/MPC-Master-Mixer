#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace mpcmixer {

// Platform-independent DSP core. It processes caller-owned mono float buffers;
// it does not open audio devices, allocate memory, or access the MPC/ALSA.
class AudioEngine {
public:
    static constexpr std::size_t kTrackCount = 16;

    struct Track {
        float gain = 1.0f; // Linear amplitude [0, 1].
        float pan = 0.0f;  // -1 = left, 0 = centre, +1 = right.
        bool mute = false;
        bool solo = false;
    };

    bool setTrack(std::size_t index, const Track& track) {
        if (index >= kTrackCount || !std::isfinite(track.gain) ||
            !std::isfinite(track.pan) || track.gain < 0.0f ||
            track.gain > 1.0f || track.pan < -1.0f || track.pan > 1.0f) {
            return false;
        }
        tracks_[index] = track;
        return true;
    }

    const Track& track(std::size_t index) const {
        return tracks_[index < kTrackCount ? index : 0];
    }

    bool setMasterGain(float gain) {
        if (!std::isfinite(gain) || gain < 0.0f || gain > 1.0f) {
            return false;
        }
        masterGain_ = gain;
        return true;
    }

    void setMasterMute(bool muted) { masterMute_ = muted; }

    float masterGain() const { return masterGain_; }
    bool masterMute() const { return masterMute_; }

    // Each input is one mono track buffer. A null input is treated as silence.
    // Output buffers are stereo, caller-owned, and overwritten for frameCount.
    // The float bus is intentionally not clipped: downstream output stages
    // must handle conversion/limiting according to the eventual audio backend.
    void process(const float* const inputs[kTrackCount],
                 float* outputLeft, float* outputRight,
                 std::size_t frameCount) const {
        if (outputLeft == nullptr || outputRight == nullptr) return;

        bool anySolo = false;
        for (const auto& trackState : tracks_) {
            anySolo = anySolo || trackState.solo;
        }

        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            float left = 0.0f;
            float right = 0.0f;
            if (!masterMute_) {
                for (std::size_t channel = 0; channel < kTrackCount; ++channel) {
                    const Track& state = tracks_[channel];
                    if (state.mute || (anySolo && !state.solo) ||
                        inputs[channel] == nullptr) {
                        continue;
                    }
                    float sample = inputs[channel][frame];
                    if (!std::isfinite(sample)) sample = 0.0f;
                    const float scaled = sample * state.gain;
                    const float leftPan = std::sqrt((1.0f - state.pan) * 0.5f);
                    const float rightPan = std::sqrt((1.0f + state.pan) * 0.5f);
                    left += scaled * leftPan;
                    right += scaled * rightPan;
                }
                left *= masterGain_;
                right *= masterGain_;
            }
            outputLeft[frame] = left;
            outputRight[frame] = right;
        }
    }

private:
    std::array<Track, kTrackCount> tracks_{};
    float masterGain_ = 1.0f;
    bool masterMute_ = false;
};


// Two mono inputs that can operate independently or as one linked stereo strip.
// This processor is deliberately device-agnostic: callers provide both buffers.
class AudioInputPair {
public:
    struct Channel {
        float gain = 1.0f;
        float pan = 0.0f;
        bool mute = false;
        bool solo = false;
    };

    struct StereoControls {
        float gain = 1.0f;
        float balance = 0.0f; // -1 attenuates right; +1 attenuates left.
        bool mute = false;
    };

    bool setChannel(std::size_t side, const Channel& channel) {
        if (side >= 2 || !std::isfinite(channel.gain) ||
            !std::isfinite(channel.pan) || channel.gain < 0.0f ||
            channel.gain > 1.0f || channel.pan < -1.0f ||
            channel.pan > 1.0f) {
            return false;
        }
        channels_[side] = channel;
        return true;
    }

    const Channel& channel(std::size_t side) const {
        return channels_[side < 2 ? side : 0];
    }

    bool setStereoControls(const StereoControls& controls) {
        if (!std::isfinite(controls.gain) ||
            !std::isfinite(controls.balance) || controls.gain < 0.0f ||
            controls.gain > 1.0f || controls.balance < -1.0f ||
            controls.balance > 1.0f) {
            return false;
        }
        stereo_ = controls;
        return true;
    }

    const StereoControls& stereoControls() const { return stereo_; }
    void setLinked(bool linked) { linked_ = linked; }
    bool linked() const { return linked_; }

    // In linked mode, left/right remain separate and stereo balance attenuates
    // only the opposite side. In unlinked mode, each input is a mono channel
    // with its own gain, pan, mute, and solo controls.
    void process(const float* inputLeft, const float* inputRight,
                 float* outputLeft, float* outputRight,
                 std::size_t frameCount) const {
        if (outputLeft == nullptr || outputRight == nullptr) return;

        if (linked_) {
            const float leftBalance = stereo_.balance > 0.0f
                ? 1.0f - stereo_.balance : 1.0f;
            const float rightBalance = stereo_.balance < 0.0f
                ? 1.0f + stereo_.balance : 1.0f;
            for (std::size_t frame = 0; frame < frameCount; ++frame) {
                const float left = inputLeft != nullptr &&
                    std::isfinite(inputLeft[frame]) ? inputLeft[frame] : 0.0f;
                const float right = inputRight != nullptr &&
                    std::isfinite(inputRight[frame]) ? inputRight[frame] : 0.0f;
                outputLeft[frame] = stereo_.mute ? 0.0f :
                    left * stereo_.gain * leftBalance;
                outputRight[frame] = stereo_.mute ? 0.0f :
                    right * stereo_.gain * rightBalance;
            }
            return;
        }

        const bool anySolo = channels_[0].solo || channels_[1].solo;
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            float leftOut = 0.0f;
            float rightOut = 0.0f;
            for (std::size_t side = 0; side < 2; ++side) {
                const Channel& state = channels_[side];
                const float* input = side == 0 ? inputLeft : inputRight;
                if (state.mute || (anySolo && !state.solo) || input == nullptr)
                    continue;
                const float sample = std::isfinite(input[frame])
                    ? input[frame] : 0.0f;
                const float scaled = sample * state.gain;
                leftOut += scaled * std::sqrt((1.0f - state.pan) * 0.5f);
                rightOut += scaled * std::sqrt((1.0f + state.pan) * 0.5f);
            }
            outputLeft[frame] = leftOut;
            outputRight[frame] = rightOut;
        }
    }

private:
    std::array<Channel, 2> channels_{};
    StereoControls stereo_{};
    bool linked_ = false;
};

} // namespace mpcmixer
