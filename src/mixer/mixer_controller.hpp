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
        AudioInputPair::StereoControls audioInputStereo{};
        std::array<AudioInputPair::Channel, 2> audioInputChannels{};
        std::array<std::array<SixBandEq::BandParameters, SixBandEq::kBandCount>, 2>
            audioInputEq{};
        std::array<float, 2> audioInputEqSampleRates{{48000.0f, 48000.0f}};
        std::array<std::array<AudioInsert*, InsertChain::kSlotCount>, 2>
            audioInputInserts{};
        bool audioInputLinked = false;
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

        // Do not allow a second pointer handoff for this slot while the first
        // one is still queued or executing. Otherwise an intermediate object
        // could be retired before the callback has finished referencing it.
        if (insertUpdatePending_[track][slot]) {
            if (!bridge_.hasApplied(insertUpdateTickets_[track][slot]))
                return false;
            insertUpdatePending_[track][slot] = false;
        }

        if (!bridge_.enqueueTrackInsert(track, slot, effect, ticket)) return false;
        inserts_[track][slot] = effect;
        insertUpdateTickets_[track][slot] = ticket;
        insertUpdatePending_[track][slot] = true;
        return true;
    }

    bool setAudioInputPairInsert(std::size_t side, std::size_t slot,
                                 AudioInsert* effect,
                                 std::uint32_t& ticket) noexcept {
        if (side >= 2 || slot >= InsertChain::kSlotCount) return false;
        if (audioInputInsertPending_[side][slot]) {
            if (!bridge_.hasApplied(audioInputInsertTickets_[side][slot]))
                return false;
            audioInputInsertPending_[side][slot] = false;
        }
        if (!bridge_.enqueueAudioInputPairInsert(side, slot, effect, ticket))
            return false;
        audioInputInserts_[side][slot] = effect;
        audioInputInsertTickets_[side][slot] = ticket;
        audioInputInsertPending_[side][slot] = true;
        return true;
    }

    bool isAudioInputPairInsertUpdateApplied(std::uint32_t ticket) const noexcept {
        return bridge_.hasApplied(ticket);
    }

    // Latest successfully queued audio command. Read immediately after a
    // successful setter on the serialized producer/control thread.
    std::uint32_t lastAudioCommandTicket() const noexcept {
        return bridge_.lastEnqueuedTicket();
    }

    bool isAudioCommandApplied(std::uint32_t ticket) const noexcept {
        return bridge_.hasApplied(ticket);
    }

    // Query an insert replacement ticket from the serialized control thread.
    // Once true, the previous object is no longer referenced by the audio engine
    // and may be retired here (never from the audio callback).
    bool isInsertUpdateApplied(std::uint32_t ticket) const noexcept {
        return bridge_.hasApplied(ticket);
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

    bool setAudioInputPairEq(
        std::size_t side, float sampleRate,
        const std::array<SixBandEq::BandParameters, SixBandEq::kBandCount>& bands) noexcept {
        if (side >= 2) return false;
        if (audioInputLinked_) {
            if (!bridge_.enqueueAudioInputPairEqState(sampleRate, bands))
                return false;
            audioInputEqBands_[0] = audioInputEqBands_[1] = bands;
            audioInputEqSampleRates_[0] = audioInputEqSampleRates_[1] = sampleRate;
        } else {
            if (!bridge_.enqueueAudioInputPairEq(side, sampleRate, bands))
                return false;
            audioInputEqBands_[side] = bands;
            audioInputEqSampleRates_[side] = sampleRate;
        }
        return true;
    }

    bool setAudioInputPairEqBand(
        std::size_t side, std::size_t band, float sampleRate,
        const SixBandEq::BandParameters& parameters) noexcept {
        if (side >= 2 || band >= SixBandEq::kBandCount) return false;
        auto next = audioInputEqBands_[side];
        next[band] = parameters;
        return setAudioInputPairEq(side, sampleRate, next);
    }

    bool setAudioInputPairLinked(bool linked) noexcept {
        if (linked == audioInputLinked_) return true;
        auto nextStereo = audioInputStereo_;
        auto nextChannels = audioInputChannels_;
        if (linked) {
            // The left strip seeds the shared stereo state when linking.
            nextStereo.gain = nextChannels[0].gain;
            nextStereo.mute = nextChannels[0].mute;
            nextStereo.solo = nextChannels[0].solo;
            nextStereo.auxSend = nextChannels[0].auxSend;
            nextChannels[1].gain = nextChannels[0].gain;
            nextChannels[1].mute = nextChannels[0].mute;
            nextChannels[1].solo = nextChannels[0].solo;
            nextChannels[1].auxSend = nextChannels[0].auxSend;
        } else {
            // Unlinking copies shared state to both sides, retaining mono pan.
            for (auto& channel : nextChannels) {
                channel.gain = nextStereo.gain;
                channel.mute = nextStereo.mute;
                channel.solo = nextStereo.solo;
                channel.auxSend = nextStereo.auxSend;
            }
        }
        auto nextEqBands = audioInputEqBands_;
        auto nextEqSampleRates = audioInputEqSampleRates_;
        if (linked) {
            // The left strip seeds the shared stereo EQ as well as fader,
            // mute, solo and Aux send state.
            nextEqBands[1] = nextEqBands[0];
            nextEqSampleRates[1] = nextEqSampleRates[0];
            if (!bridge_.enqueueAudioInputPairState(
                    linked, nextStereo, nextChannels, nextEqSampleRates[0],
                    &nextEqBands[0])) return false;
        } else if (!bridge_.enqueueAudioInputPairState(
                       linked, nextStereo, nextChannels)) {
            return false;
        }
        audioInputLinked_ = linked;
        audioInputStereo_ = nextStereo;
        audioInputChannels_ = nextChannels;
        audioInputEqBands_ = nextEqBands;
        audioInputEqSampleRates_ = nextEqSampleRates;
        return true;
    }

    bool setAudioInputPairStereoGain(int value) noexcept {
        if (!audioInputLinked_ || value < 0 || value > kFaderMaximum) return false;
        auto next = audioInputStereo_;
        next.gain = static_cast<float>(value) / 100.0f;
        if (!bridge_.enqueueAudioInputPairStereoControls(next)) return false;
        audioInputStereo_ = next;
        for (auto& channel : audioInputChannels_) channel.gain = next.gain;
        return true;
    }

    bool setAudioInputPairBalance(int value) noexcept {
        if (!audioInputLinked_ || value < kPanMinimum || value > kPanMaximum)
            return false;
        auto next = audioInputStereo_;
        next.balance = static_cast<float>(value) / 100.0f;
        if (!bridge_.enqueueAudioInputPairStereoControls(next)) return false;
        audioInputStereo_ = next;
        return true;
    }

    bool setAudioInputPairStereoMute(bool muted) noexcept {
        if (!audioInputLinked_) return false;
        auto next = audioInputStereo_;
        next.mute = muted;
        if (!bridge_.enqueueAudioInputPairStereoControls(next)) return false;
        audioInputStereo_ = next;
        for (auto& channel : audioInputChannels_) channel.mute = muted;
        return true;
    }

    bool setAudioInputPairChannelGain(std::size_t side, int value) noexcept {
        if (side >= 2 || value < 0 || value > kFaderMaximum) return false;
        const float gain = static_cast<float>(value) / 100.0f;
        if (audioInputLinked_) {
            auto next = audioInputStereo_;
            next.gain = gain;
            if (!bridge_.enqueueAudioInputPairStereoControls(next)) return false;
            audioInputStereo_ = next;
            for (auto& channel : audioInputChannels_) channel.gain = gain;
            return true;
        }
        auto next = audioInputChannels_[side];
        next.gain = gain;
        if (!bridge_.enqueueAudioInputPairChannel(side, next)) return false;
        audioInputChannels_[side] = next;
        return true;
    }

    bool setAudioInputPairAuxSend(std::size_t side, std::size_t bus,
                                  float level) noexcept {
        if (side >= 2 || bus >= AudioEngine::kAuxBusCount ||
            !std::isfinite(level) || level < 0.0f || level > 1.0f)
            return false;
        if (audioInputLinked_) {
            auto next = audioInputStereo_;
            next.auxSend[bus] = level;
            if (!bridge_.enqueueAudioInputPairStereoControls(next)) return false;
            audioInputStereo_ = next;
            for (auto& channel : audioInputChannels_)
                channel.auxSend[bus] = level;
            return true;
        }
        auto next = audioInputChannels_[side];
        next.auxSend[bus] = level;
        if (!bridge_.enqueueAudioInputPairChannel(side, next)) return false;
        audioInputChannels_[side] = next;
        return true;
    }

    bool setAudioInputPairChannelPan(std::size_t side, int value) noexcept {
        if (audioInputLinked_ || side >= 2 ||
            value < kPanMinimum || value > kPanMaximum) return false;
        auto next = audioInputChannels_[side];
        next.pan = static_cast<float>(value) / 100.0f;
        if (!bridge_.enqueueAudioInputPairChannel(side, next)) return false;
        audioInputChannels_[side] = next;
        return true;
    }

    bool setAudioInputPairChannelMute(std::size_t side, bool muted) noexcept {
        if (side >= 2) return false;
        if (audioInputLinked_) {
            auto next = audioInputStereo_;
            next.mute = muted;
            if (!bridge_.enqueueAudioInputPairStereoControls(next)) return false;
            audioInputStereo_ = next;
            for (auto& channel : audioInputChannels_) channel.mute = muted;
            return true;
        }
        auto next = audioInputChannels_[side];
        next.mute = muted;
        if (!bridge_.enqueueAudioInputPairChannel(side, next)) return false;
        audioInputChannels_[side] = next;
        return true;
    }

    bool setAudioInputPairChannelSolo(std::size_t side, bool soloed) noexcept {
        if (side >= 2) return false;
        if (audioInputLinked_) {
            auto next = audioInputStereo_;
            next.solo = soloed;
            if (!bridge_.enqueueAudioInputPairStereoControls(next)) return false;
            audioInputStereo_ = next;
            for (auto& channel : audioInputChannels_) channel.solo = soloed;
            return true;
        }
        auto next = audioInputChannels_[side];
        next.solo = soloed;
        if (!bridge_.enqueueAudioInputPairChannel(side, next)) return false;
        audioInputChannels_[side] = next;
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
    bool desiredAudioInputPairLinked() const noexcept { return audioInputLinked_; }
    const AudioInputPair::StereoControls& desiredAudioInputPairStereo() const noexcept {
        return audioInputStereo_;
    }
    const AudioInputPair::Channel& desiredAudioInputPairChannel(
        std::size_t side) const noexcept {
        return audioInputChannels_[side < 2 ? side : 0];
    }

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
        snapshot.audioInputStereo = audioInputStereo_;
        snapshot.audioInputChannels = audioInputChannels_;
        snapshot.audioInputEq = audioInputEqBands_;
        snapshot.audioInputEqSampleRates = audioInputEqSampleRates_;
        snapshot.audioInputInserts = audioInputInserts_;
        snapshot.audioInputLinked = audioInputLinked_;
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
    std::array<std::array<std::uint32_t, InsertChain::kSlotCount>,
               AudioEngine::kTrackCount> insertUpdateTickets_{};
    std::array<std::array<bool, InsertChain::kSlotCount>,
               AudioEngine::kTrackCount> insertUpdatePending_{};
    std::array<AudioEngine::AuxReturn, AudioEngine::kAuxBusCount> auxReturns_{};
    float masterGain_ = 1.0f;
    bool masterMute_ = false;
    AudioInputPair::StereoControls audioInputStereo_{};
    std::array<AudioInputPair::Channel, 2> audioInputChannels_{};
    std::array<std::array<SixBandEq::BandParameters, SixBandEq::kBandCount>, 2>
        audioInputEqBands_{{SixBandEq::defaultBandParameters(),
                            SixBandEq::defaultBandParameters()}};
    std::array<float, 2> audioInputEqSampleRates_{{48000.0f, 48000.0f}};
    std::array<std::array<AudioInsert*, InsertChain::kSlotCount>, 2>
        audioInputInserts_{};
    std::array<std::array<std::uint32_t, InsertChain::kSlotCount>, 2>
        audioInputInsertTickets_{};
    std::array<std::array<bool, InsertChain::kSlotCount>, 2>
        audioInputInsertPending_{};
    bool audioInputLinked_ = false;
};

} // namespace mpcmixer
