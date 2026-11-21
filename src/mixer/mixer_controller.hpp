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

    explicit MixerController(AudioEngineCommandBridge& bridge) noexcept
        : bridge_(bridge) {}

    bool setTrackFader(std::size_t track, int value) noexcept {
        if (value > kFaderMaximum || track >= AudioEngine::kTrackCount)
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
            bus >= AudioEngine::kAuxBusCount || percent > kAuxMaximum)
            return false;
        AudioEngine::Track next = tracks_[track];
        next.auxSend[bus] = static_cast<float>(percent) / 100.0f;
        return publishTrack(track, next);
    }

    bool setMasterFader(int value) noexcept {
        if (value > kFaderMaximum) return false;
        return bridge_.enqueueMasterGain(static_cast<float>(value) / 100.0f);
    }

    bool setMasterMute(bool muted) noexcept {
        return bridge_.enqueueMasterMute(muted);
    }

    const AudioEngine::Track& desiredTrack(std::size_t track) const noexcept {
        return tracks_[track < AudioEngine::kTrackCount ? track : 0];
    }

private:
    bool publishTrack(std::size_t index,
                      const AudioEngine::Track& next) noexcept {
        if (!bridge_.enqueueTrack(index, next)) return false;
        tracks_[index] = next;
        return true;
    }

    AudioEngineCommandBridge& bridge_;
    std::array<AudioEngine::Track, AudioEngine::kTrackCount> tracks_{};
};

} // namespace mpcmixer
