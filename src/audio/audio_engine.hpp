#pragma once

#include "six_band_eq.hpp"
#include "insert_effects.hpp"

#include <array>
#include <cmath>
#include <cstddef>

namespace mpcmixer {

// Platform-independent DSP core. It processes caller-owned mono float buffers;
// it does not open audio devices, allocate memory, or access the MPC/ALSA.
// Setters are control-thread operations and must not race process() calls;
// the host must publish parameter changes at a safe block boundary.
class AudioEngine {
public:
    static constexpr std::size_t kTrackCount = 16;
    static constexpr std::size_t kAuxBusCount = 4;
    static constexpr float kMaxFaderGain = 1.27f; // +2.1 dB, matching the prototype's 127/100 fader range.

    struct Track {
        float gain = 1.0f; // Linear amplitude [0, +2.1 dB].
        float pan = 0.0f;  // -1 = left, 0 = centre, +1 = right.
        bool mute = false;
        bool solo = false;
        // Post-fader, post-pan send levels for the four stereo Aux buses.
        std::array<float, kAuxBusCount> auxSend{};
    };

    struct AuxReturn {
        float gain = 1.0f;
        float balance = 0.0f; // -1 attenuates right; +1 attenuates left.
        bool mute = false;
    };

    bool setTrack(std::size_t index, const Track& track) {
        if (index >= kTrackCount || !std::isfinite(track.gain) ||
            !std::isfinite(track.pan) || track.gain < 0.0f ||
            track.gain > kMaxFaderGain || track.pan < -1.0f || track.pan > 1.0f) {
            return false;
        }
        for (float send : track.auxSend) {
            if (!std::isfinite(send) || send < 0.0f || send > 1.0f) {
                return false;
            }
        }
        tracks_[index] = track;
        return true;
    }

    const Track& track(std::size_t index) const {
        return tracks_[index < kTrackCount ? index : 0];
    }

    // Prepare EQ coefficients on the control thread, never from process().
    bool setTrackEq(
        std::size_t index, float sampleRate,
        const std::array<SixBandEq::BandParameters, SixBandEq::kBandCount>& bands) {
        if (index >= kTrackCount) return false;
        return equalizers_[index].configure(sampleRate, bands);
    }

    bool setTrackEqPrepared(
        std::size_t index,
        const SixBandEq::PreparedConfiguration& prepared) noexcept {
        if (index >= kTrackCount) return false;
        return equalizers_[index].applyPrepared(prepared);
    }

    // Non-owning insert pointers: keep effects alive until processing stops.
    bool setTrackInsert(std::size_t trackIndex, std::size_t slot,
                        AudioInsert* effect) noexcept {
        if (trackIndex >= kTrackCount) return false;
        return inserts_[trackIndex].setSlot(slot, effect);
    }

    const SixBandEq& trackEq(std::size_t index) const {
        return equalizers_[index < kTrackCount ? index : 0];
    }

    bool setAuxReturn(std::size_t index, const AuxReturn& state) {
        if (index >= kAuxBusCount || !std::isfinite(state.gain) ||
            !std::isfinite(state.balance) || state.gain < 0.0f ||
            state.gain > kMaxFaderGain || state.balance < -1.0f ||
            state.balance > 1.0f) {
            return false;
        }
        auxReturns_[index] = state;
        return true;
    }

    const AuxReturn& auxReturn(std::size_t index) const {
        return auxReturns_[index < kAuxBusCount ? index : 0];
    }

    bool setMasterGain(float gain) {
        if (!std::isfinite(gain) || gain < 0.0f || gain > kMaxFaderGain) {
            return false;
        }
        masterGain_ = gain;
        return true;
    }

    void setMasterMute(bool muted) { masterMute_ = muted; }

    float masterGain() const { return masterGain_; }
    bool masterMute() const { return masterMute_; }

    bool hasSoloedTrack() const noexcept {
        for (const auto& state : tracks_) {
            if (state.solo) return true;
        }
        return false;
    }

    // Each input is one mono track buffer. A null input is treated as silence.
    // Output buffers are stereo, caller-owned, and overwritten for frameCount.
    // The float bus is intentionally not clipped: downstream output stages
    // must handle conversion/limiting according to the eventual audio backend.
    // Four independent stereo Aux buses. Sends are post-fader and post-pan,
    // but pre-master, so the master fader does not alter monitor/effect sends.
    // Each output is caller-owned; a null Aux output disables only that side/bus.
    // Audio-thread entry point: caller owns all buffers; no allocation or locks.
    void processWithAuxAndReturns(
        const float* const inputs[kTrackCount],
        float* outputLeft, float* outputRight,
        float* const auxLeft[kAuxBusCount],
        float* const auxRight[kAuxBusCount],
        const float* const auxReturnLeft[kAuxBusCount],
        const float* const auxReturnRight[kAuxBusCount],
        std::size_t frameCount, bool externalSolo = false) {
        if (outputLeft == nullptr || outputRight == nullptr) return;

        const bool anySolo = externalSolo || hasSoloedTrack();

        // Pan is constant for the duration of this block. Calculate the
        // constant-power gains once instead of taking two square roots per
        // track per sample in the real-time loop.
        std::array<float, kTrackCount> leftPanGains{};
        std::array<float, kTrackCount> rightPanGains{};
        for (std::size_t channel = 0; channel < kTrackCount; ++channel) {
            const float pan = tracks_[channel].pan;
            leftPanGains[channel] = std::sqrt((1.0f - pan) * 0.5f);
            rightPanGains[channel] = std::sqrt((1.0f + pan) * 0.5f);
        }

        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            float left = 0.0f;
            float right = 0.0f;
            std::array<float, kAuxBusCount> sendLeft{};
            std::array<float, kAuxBusCount> sendRight{};
            for (std::size_t channel = 0; channel < kTrackCount; ++channel) {
                const Track& state = tracks_[channel];
                const float* input = inputs == nullptr ? nullptr : inputs[channel];
                if (input == nullptr) continue;
                float sample = input[frame];
                if (!std::isfinite(sample)) sample = 0.0f;
                sample = equalizers_[channel].processSample(sample);
                sample = inserts_[channel].processSample(sample);
                if (state.mute || (anySolo && !state.solo)) continue;
                const float scaled = sample * state.gain;
                const float leftPan = leftPanGains[channel];
                const float rightPan = rightPanGains[channel];
                if (!masterMute_) {
                    left += scaled * leftPan;
                    right += scaled * rightPan;
                }
                for (std::size_t bus = 0; bus < kAuxBusCount; ++bus) {
                    sendLeft[bus] += scaled * leftPan * state.auxSend[bus];
                    sendRight[bus] += scaled * rightPan * state.auxSend[bus];
                }
            }
            if (!masterMute_) {
                for (std::size_t bus = 0; bus < kAuxBusCount; ++bus) {
                    const AuxReturn& state = auxReturns_[bus];
                    if (state.mute) continue;
                    const float returnLeft = auxReturnLeft != nullptr &&
                        auxReturnLeft[bus] != nullptr &&
                        std::isfinite(auxReturnLeft[bus][frame])
                        ? auxReturnLeft[bus][frame] : 0.0f;
                    const float returnRight = auxReturnRight != nullptr &&
                        auxReturnRight[bus] != nullptr &&
                        std::isfinite(auxReturnRight[bus][frame])
                        ? auxReturnRight[bus][frame] : 0.0f;
                    const float leftBalance = state.balance > 0.0f
                        ? 1.0f - state.balance : 1.0f;
                    const float rightBalance = state.balance < 0.0f
                        ? 1.0f + state.balance : 1.0f;
                    left += returnLeft * state.gain * leftBalance;
                    right += returnRight * state.gain * rightBalance;
                }
            }
            outputLeft[frame] = masterMute_ ? 0.0f : left * masterGain_;
            outputRight[frame] = masterMute_ ? 0.0f : right * masterGain_;
            for (std::size_t bus = 0; bus < kAuxBusCount; ++bus) {
                if (auxLeft != nullptr && auxLeft[bus] != nullptr)
                    auxLeft[bus][frame] = sendLeft[bus];
                if (auxRight != nullptr && auxRight[bus] != nullptr)
                    auxRight[bus][frame] = sendRight[bus];
            }
        }
    }

    // Send-only overload for callers without Aux return buffers.
    void processWithAux(
        const float* const inputs[kTrackCount],
        float* outputLeft, float* outputRight,
        float* const auxLeft[kAuxBusCount],
        float* const auxRight[kAuxBusCount],
        std::size_t frameCount) {
        processWithAuxAndReturns(inputs, outputLeft, outputRight,
                                 auxLeft, auxRight, nullptr, nullptr,
                                 frameCount);
    }

    // Backward-compatible main stereo mix without Aux output buffers.
    void process(const float* const inputs[kTrackCount],
                 float* outputLeft, float* outputRight,
                 std::size_t frameCount) {
        processWithAuxAndReturns(inputs, outputLeft, outputRight,
                                 nullptr, nullptr, nullptr, nullptr,
                                 frameCount);
    }

private:
    std::array<Track, kTrackCount> tracks_{};
    std::array<SixBandEq, kTrackCount> equalizers_{};
    std::array<InsertChain, kTrackCount> inserts_{};
    std::array<AuxReturn, kAuxBusCount> auxReturns_{};
    float masterGain_ = 1.0f;
    bool masterMute_ = false;
};


// Two mono inputs that can operate independently or as one linked stereo strip.
// This processor is deliberately device-agnostic: callers provide both buffers.
// Its linked-stereo and independent-mono modes are covered by host-side tests.
class AudioInputPair {
public:
    struct Channel {
        float gain = 1.0f;
        float pan = 0.0f;
        bool mute = false;
        bool solo = false;
        std::array<float, AudioEngine::kAuxBusCount> auxSend{};
    };

    struct StereoControls {
        float gain = 1.0f; // Linear amplitude, maximum +2.1 dB.
        float balance = 0.0f; // -1 attenuates right; +1 attenuates left.
        bool mute = false;
        bool solo = false;
        std::array<float, AudioEngine::kAuxBusCount> auxSend{};
    };

    bool setChannel(std::size_t side, const Channel& channel) {
        if (side >= 2 || !std::isfinite(channel.gain) ||
            !std::isfinite(channel.pan) || channel.gain < 0.0f ||
            channel.gain > AudioEngine::kMaxFaderGain || channel.pan < -1.0f ||
            channel.pan > 1.0f) return false;
        for (float send : channel.auxSend) {
            if (!std::isfinite(send) || send < 0.0f || send > 1.0f)
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
            controls.gain > AudioEngine::kMaxFaderGain ||
            controls.balance < -1.0f || controls.balance > 1.0f)
            return false;
        for (float send : controls.auxSend) {
            if (!std::isfinite(send) || send < 0.0f || send > 1.0f)
                return false;
        }
        stereo_ = controls;
        return true;
    }

    const StereoControls& stereoControls() const { return stereo_; }

    // EQ coefficients are prepared on the control thread and copied at a
    // block boundary through AudioEngineCommandBridge.
    bool setEq(std::size_t side, float sampleRate,
               const std::array<SixBandEq::BandParameters,
                                SixBandEq::kBandCount>& bands) {
        if (side >= 2) return false;
        return equalizers_[side].configure(sampleRate, bands);
    }

    bool setEqPrepared(
        std::size_t side,
        const SixBandEq::PreparedConfiguration& prepared) noexcept {
        if (side >= 2) return false;
        return equalizers_[side].applyPrepared(prepared);
    }

    // Insert pointers are non-owning. Keep the previous and replacement
    // objects alive until the corresponding command ticket is acknowledged.
    bool setInsert(std::size_t side, std::size_t slot,
                   AudioInsert* effect) noexcept {
        if (side >= 2) return false;
        return inserts_[side].setSlot(slot, effect);
    }

    void setLinked(bool linked) { linked_ = linked; }
    bool linked() const { return linked_; }

    bool hasSolo() const noexcept {
        return linked_ ? stereo_.solo : (channels_[0].solo || channels_[1].solo);
    }

    // Replaces the supplied output buffers with the pair's stereo signal.
    void process(const float* inputLeft, const float* inputRight,
                 float* outputLeft, float* outputRight,
                 std::size_t frameCount) const {
        processImpl(inputLeft, inputRight, outputLeft, outputRight,
                    frameCount, false, 1.0f, false, false, nullptr, nullptr);
    }

    // Adds the pair to an existing stereo mix. postGain/postMute represent
    // the shared Master stage. Aux sends are post-input-fader/pan but pre-Master,
    // matching the regular track-strip send convention.
    void processAndMix(const float* inputLeft, const float* inputRight,
                       float* outputLeft, float* outputRight,
                       std::size_t frameCount, float postGain,
                       bool postMute, bool otherSolo = false,
                       float* const* auxLeft = nullptr,
                       float* const* auxRight = nullptr) const {
        if (!std::isfinite(postGain) || postGain < 0.0f ||
            postGain > AudioEngine::kMaxFaderGain) return;
        processImpl(inputLeft, inputRight, outputLeft, outputRight,
                    frameCount, true, postGain, postMute, otherSolo,
                    auxLeft, auxRight);
    }

private:
    void processImpl(const float* inputLeft, const float* inputRight,
                     float* outputLeft, float* outputRight,
                     std::size_t frameCount, bool add,
                     float postGain, bool postMute, bool otherSolo,
                     float* const* auxLeft,
                     float* const* auxRight) const {
        if (outputLeft == nullptr || outputRight == nullptr) return;

        if (linked_) {
            const float leftBalance = stereo_.balance > 0.0f
                ? 1.0f - stereo_.balance : 1.0f;
            const float rightBalance = stereo_.balance < 0.0f
                ? 1.0f + stereo_.balance : 1.0f;
            const bool soloSuppressed = otherSolo && !stereo_.solo;
            for (std::size_t frame = 0; frame < frameCount; ++frame) {
                float left = inputLeft != nullptr &&
                    std::isfinite(inputLeft[frame]) ? inputLeft[frame] : 0.0f;
                float right = inputRight != nullptr &&
                    std::isfinite(inputRight[frame]) ? inputRight[frame] : 0.0f;
                left = inserts_[0].processSample(equalizers_[0].processSample(left, 0));
                right = inserts_[1].processSample(equalizers_[1].processSample(right, 1));

                const bool audible = !stereo_.mute && !postMute && !soloSuppressed;
                const float processedLeft = audible
                    ? left * stereo_.gain * postGain * leftBalance : 0.0f;
                const float processedRight = audible
                    ? right * stereo_.gain * postGain * rightBalance : 0.0f;
                if (add) {
                    outputLeft[frame] += processedLeft;
                    outputRight[frame] += processedRight;
                } else {
                    outputLeft[frame] = processedLeft;
                    outputRight[frame] = processedRight;
                }

                // Master mute/gain do not change pre-Master Aux sends.
                if (!stereo_.mute && !soloSuppressed) {
                    for (std::size_t bus = 0; bus < AudioEngine::kAuxBusCount; ++bus) {
                        if (auxLeft != nullptr && auxLeft[bus] != nullptr)
                            auxLeft[bus][frame] += left * stereo_.gain *
                                leftBalance * stereo_.auxSend[bus];
                        if (auxRight != nullptr && auxRight[bus] != nullptr)
                            auxRight[bus][frame] += right * stereo_.gain *
                                rightBalance * stereo_.auxSend[bus];
                    }
                }
            }
            return;
        }

        const bool anySolo = channels_[0].solo || channels_[1].solo;
        const bool soloSuppressed = otherSolo && !anySolo;
        std::array<float, 2> leftPanGains{};
        std::array<float, 2> rightPanGains{};
        for (std::size_t side = 0; side < 2; ++side) {
            const float pan = channels_[side].pan;
            leftPanGains[side] = std::sqrt((1.0f - pan) * 0.5f);
            rightPanGains[side] = std::sqrt((1.0f + pan) * 0.5f);
        }

        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            float leftOut = 0.0f;
            float rightOut = 0.0f;
            std::array<float, AudioEngine::kAuxBusCount> sendLeft{};
            std::array<float, AudioEngine::kAuxBusCount> sendRight{};
            for (std::size_t side = 0; side < 2; ++side) {
                const Channel& state = channels_[side];
                const float* input = side == 0 ? inputLeft : inputRight;
                if (input == nullptr) continue;
                float sample = std::isfinite(input[frame]) ? input[frame] : 0.0f;
                sample = equalizers_[side].processSample(sample, side);
                sample = inserts_[side].processSample(sample);
                if (state.mute || (anySolo && !state.solo) || soloSuppressed)
                    continue;

                const float preMaster = sample * state.gain;
                if (!postMute) {
                    const float scaled = preMaster * postGain;
                    leftOut += scaled * leftPanGains[side];
                    rightOut += scaled * rightPanGains[side];
                }
                for (std::size_t bus = 0; bus < AudioEngine::kAuxBusCount; ++bus) {
                    sendLeft[bus] += preMaster * leftPanGains[side] *
                                     state.auxSend[bus];
                    sendRight[bus] += preMaster * rightPanGains[side] *
                                      state.auxSend[bus];
                }
            }
            if (add) {
                outputLeft[frame] += leftOut;
                outputRight[frame] += rightOut;
            } else {
                outputLeft[frame] = leftOut;
                outputRight[frame] = rightOut;
            }
            for (std::size_t bus = 0; bus < AudioEngine::kAuxBusCount; ++bus) {
                if (auxLeft != nullptr && auxLeft[bus] != nullptr)
                    auxLeft[bus][frame] += sendLeft[bus];
                if (auxRight != nullptr && auxRight[bus] != nullptr)
                    auxRight[bus][frame] += sendRight[bus];
            }
        }
    }

    std::array<Channel, 2> channels_{};
    StereoControls stereo_{};
    mutable std::array<SixBandEq, 2> equalizers_{};
    mutable std::array<InsertChain, 2> inserts_{};
    bool linked_ = false;
};
} // namespace mpcmixer
