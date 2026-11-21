#pragma once

#include "../audio/audio_control_bridge.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mpcmixer {

// Framework-neutral control adapter for a native mixer UI. It owns the
// producer-side desired-state snapshot and is intended to be called by one
// serialized UI/control thread. It never touches the audio engine directly.
class MixerController {
public:
    static constexpr std::uint8_t kFaderMaximum = 127;
    static constexpr std::int16_t kPanMinimum = -100;
    static constexpr std::int16_t kPanMaximum = 100;
    static constexpr std::uint8_t kAuxMaximum = 100;

    struct TrackState {
        AudioEngine::Track controls{};
        std::array<SixBandEq::BandParameters, SixBandEq::kBandCount> eq =
            SixBandEq::defaultBandParameters();
        std::array<AudioInsert*, InsertChain::kSlotCount> inserts{};
    };

    struct DesiredStateSnapshot {
        std::array<TrackState, AudioEngine::kTrackCount> tracks{};
        std::array<AudioEngine::AuxReturn, AudioEngine::kAuxBusCount> auxReturns{};
        float masterGain = 1.0f;
        bool masterMute = false;
    };

    explicit MixerController(AudioEngineCommandBridge& bridge) noexcept
        : bridge_(bridge), eqBands_(defaultEqState()) {}

    bool setTrackFader(std::size_t track, int value) noexcept {
        if (value < 0 || value > kFaderMaximum || track >= AudioEngine::kTrackCount)
            return false;
        AudioEngine::Track next = tracks_[track];
        next.gain = static_cast<float>(value) / 100.0f;
        return publishTrack(track, next);
    }

    bool setTrackPan(std::size_t track, int value) noexcept {
        if (value < kPanMinimum || value > kPanMaximum ||
            track >= AudioEngine::kTrackCount) return false;
        AudioEngine::Track next = tracks_[track];
        next.pan = static_cast<float>(value) / 100.0f;
        return publishTrack(track, next);
    }

    bool setTrackMute(std::size_t track, bool muted) noexcept {
        if (track >= AudioEngine::kTrackCount) return false;
        AudioEngine::Track next = tracks_[track];
        next.mute = muted;
        return publishTrack(track, next);
    }

    bool setTrackSolo(std::size_t track, bool soloed) noexcept {
        if (track >= AudioEngine::kTrackCount) return false;
        AudioEngine::Track next = tracks_[track];
        next.solo = soloed;
        return publishTrack(track, next);
    }

    bool setTrackAuxSend(std::size_t track, std::size_t bus,
                         int percent) noexcept {
        if (track >= AudioEngine::kTrackCount ||
            bus >= AudioEngine::kAuxBusCount || percent < 0 ||
            percent > kAuxMaximum)
            return false;
        AudioEngine::Track next = tracks_[track];
        next.auxSend[bus] = static_cast<float>(percent) / 100.0f;
        return publishTrack(track, next);
    }

    bool setTrackEq(
        std::size_t track, float sampleRate,
        const std::array<SixBandEq::BandParameters, SixBandEq::kBandCount>& bands) noexcept {
        if (track >= AudioEngine::kTrackCount ||
            !bridge_.enqueueTrackEq(track, sampleRate, bands)) return false;
        eqBands_[track] = bands;
        return true;
    }

    // Convenience path for a UI editing one EQ band at a time. The full
    // six-band configuration is re-enqueued so the audio thread still sees
    // one atomic prepared configuration at a block boundary.
    bool setTrackEqBand(
        std::size_t track, std::size_t band, float sampleRate,
        const SixBandEq::BandParameters& parameters) noexcept {
        if (track >= AudioEngine::kTrackCount ||
            band >= SixBandEq::kBandCount) return false;
        auto next = eqBands_[track];
        next[band] = parameters;
        if (!bridge_.enqueueTrackEq(track, sampleRate, next)) return false;
        eqBands_[track] = next;
        return true;
    }

    // Insert objects are non-owning. Keep the current and replacement
    // objects alive until bridge.hasApplied(ticket) confirms the block-boundary
    // swap; retire the old object only on this control thread after that ack.
    bool setTrackInsert(std::size_t track, std::size_t slot,
                        AudioInsert* effect, std::uint32_t& ticket) noexcept {
        if (track >= AudioEngine::kTrackCount ||
            slot >= InsertChain::kSlotCount) return false;
        if (!bridge_.enqueueTrackInsert(track, slot, effect, ticket)) return false;
        inserts_[track][slot] = effect;
        return true;
    }

    bool setAuxReturn(std::size_t bus,
                      const AudioEngine::AuxReturn& state) noexcept {
        if (bus >= AudioEngine::kAuxBusCount) return false;
        if (!bridge_.enqueueAuxReturn(bus, state)) return false;
        auxReturns_[bus] = state;
        return true;
    }

    bool setMasterFader(int value) noexcept {
        if (value < 0 || value > kFaderMaximum) return false;
        const float gain = static_cast<float>(value) / 100.0f;
        if (!bridge_.enqueueMasterGain(gain)) return false;
        masterGain_ = gain;
        return true;
    }

    bool setMasterMute(bool muted) noexcept {
        if (!bridge_.enqueueMasterMute(muted)) return false;
        masterMute_ = muted;
        return true;
    }

    const AudioEngine::Track& desiredTrack(std::size_t track) const noexcept {
        return tracks_[track < AudioEngine::kTrackCount ? track : 0];
    }

    const AudioEngine::AuxReturn& desiredAuxReturn(std::size_t bus) const noexcept {
        return auxReturns_[bus < AudioEngine::kAuxBusCount ? bus : 0];
    }

    const std::array<SixBandEq::BandParameters, SixBandEq::kBandCount>&
    desiredTrackEq(std::size_t track) const noexcept {
        return eqBands_[track < AudioEngine::kTrackCount ? track : 0];
    }

    float desiredMasterGain() const noexcept { return masterGain_; }
    bool desiredMasterMute() const noexcept { return masterMute_; }

    // Copy the complete producer-side UI state. Call on the serialized
    // control thread; this is not a cross-thread/audio-thread snapshot.
    DesiredStateSnapshot desiredStateSnapshot() const noexcept {
        DesiredStateSnapshot snapshot;
        for (std::size_t track = 0; track < AudioEngine::kTrackCount; ++track) {
            snapshot.tracks[track].controls = tracks_[track];
            snapshot.tracks[track].eq = eqBands_[track];
            snapshot.tracks[track].inserts = inserts_[track];
        }
        snapshot.auxReturns = auxReturns_;
        snapshot.masterGain = masterGain_;
        snapshot.masterMute = masterMute_;
        return snapshot;
    }

private:
    static std::array<std::array<SixBandEq::BandParameters, SixBandEq::kBandCount>,
                      AudioEngine::kTrackCount> defaultEqState() noexcept {
        std::array<std::array<SixBandEq::BandParameters, SixBandEq::kBandCount>,
                   AudioEngine::kTrackCount> state{};
        const auto defaults = SixBandEq::defaultBandParameters();
        for (auto& trackBands : state) trackBands = defaults;
        return state;
    }

    bool publishTrack(std::size_t index,
                      const AudioEngine::Track& next) noexcept {
        if (!bridge_.enqueueTrack(index, next)) return false;
        tracks_[index] = next;
        return true;
    }

    AudioEngineCommandBridge& bridge_;
    std::array<AudioEngine::Track, AudioEngine::kTrackCount> tracks_{};
    std::array<std::array<SixBandEq::BandParameters, SixBandEq::kBandCount>,
               AudioEngine::kTrackCount> eqBands_{};
    std::array<std::array<AudioInsert*, InsertChain::kSlotCount>,
               AudioEngine::kTrackCount> inserts_{};
    std::array<AudioEngine::AuxReturn, AudioEngine::kAuxBusCount> auxReturns_{};
    float masterGain_ = 1.0f;
    bool masterMute_ = false;
};

} // namespace mpcmixer
