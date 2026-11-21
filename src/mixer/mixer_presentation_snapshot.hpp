#pragma once

#include "mixer_controller.hpp"

#include <array>
#include <cmath>
#include <cstdint>

namespace mpcmixer {

// UI-facing value object. It deliberately contains no engine or insert
// pointers: consumers can render it or copy it across a future IPC boundary.
struct MixerPresentationSnapshot {
    static constexpr std::size_t kTrackCount = AudioEngine::kTrackCount;
    static constexpr std::size_t kAuxBusCount = AudioEngine::kAuxBusCount;
    static constexpr std::size_t kEqBandCount = SixBandEq::kBandCount;
    static constexpr std::size_t kInsertSlotCount = InsertChain::kSlotCount;

    struct Track {
        std::uint8_t faderValue = 100;
        std::int16_t panPercent = 0;
        bool mute = false;
        bool solo = false;
        std::array<std::uint8_t, kAuxBusCount> auxSendPercent{};
        std::array<SixBandEq::BandParameters, kEqBandCount> eq{};
        std::array<bool, kInsertSlotCount> hasInsert{};
    };

    struct AuxReturn {
        std::uint8_t levelValue = 100;
        std::int16_t balancePercent = 0;
        bool mute = false;
    };

    std::uint64_t sequence = 0;
    std::array<Track, kTrackCount> tracks{};
    std::array<AuxReturn, kAuxBusCount> auxReturns{};
    std::uint8_t masterFaderValue = 100;
    bool masterMute = false;
};

inline MixerPresentationSnapshot makeMixerPresentationSnapshot(
    const MixerController::DesiredStateSnapshot& desired,
    std::uint64_t sequence) noexcept {
    MixerPresentationSnapshot result;
    result.sequence = sequence;

    for (std::size_t index = 0; index < AudioEngine::kTrackCount; ++index) {
        const auto& source = desired.tracks[index];
        auto& target = result.tracks[index];
        target.faderValue = static_cast<std::uint8_t>(
            std::lround(source.controls.gain * 100.0f));
        target.panPercent = static_cast<std::int16_t>(
            std::lround(source.controls.pan * 100.0f));
        target.mute = source.controls.mute;
        target.solo = source.controls.solo;
        for (std::size_t bus = 0; bus < AudioEngine::kAuxBusCount; ++bus) {
            target.auxSendPercent[bus] = static_cast<std::uint8_t>(
                std::lround(source.controls.auxSend[bus] * 100.0f));
        }
        target.eq = source.eq;
        for (std::size_t slot = 0; slot < InsertChain::kSlotCount; ++slot)
            target.hasInsert[slot] = source.inserts[slot] != nullptr;
    }

    for (std::size_t bus = 0; bus < AudioEngine::kAuxBusCount; ++bus) {
        const auto& source = desired.auxReturns[bus];
        auto& target = result.auxReturns[bus];
        target.levelValue = static_cast<std::uint8_t>(
            std::lround(source.gain * 100.0f));
        target.balancePercent = static_cast<std::int16_t>(
            std::lround(source.balance * 100.0f));
        target.mute = source.mute;
    }
    result.masterFaderValue = static_cast<std::uint8_t>(
        std::lround(desired.masterGain * 100.0f));
    result.masterMute = desired.masterMute;
    return result;
}

// Called explicitly by the serialized UI/control thread; no timer, thread,
// lock, IPC, or cross-thread delivery guarantee is provided.
class MixerPresentationPublisher {
public:
    explicit MixerPresentationPublisher(const MixerController& controller) noexcept
        : controller_(controller) {}

    MixerPresentationSnapshot publish() noexcept {
        ++sequence_;
        if (sequence_ == 0) ++sequence_;
        return makeMixerPresentationSnapshot(
            controller_.desiredStateSnapshot(), sequence_);
    }

    std::uint64_t lastSequence() const noexcept { return sequence_; }

private:
    const MixerController& controller_;
    std::uint64_t sequence_ = 0;
};

} // namespace mpcmixer
