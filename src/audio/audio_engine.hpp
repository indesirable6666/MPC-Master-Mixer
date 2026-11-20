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

} // namespace mpcmixer
