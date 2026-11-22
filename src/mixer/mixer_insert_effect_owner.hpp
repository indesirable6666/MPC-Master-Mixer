#pragma once
#include "mixer_controller.hpp"
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <utility>

namespace mpcmixer {

// Control-thread owner for insert pointers published through MixerController.
// Stop and shut down the audio host before destroying this owner.
class MixerInsertEffectOwner {
public:
    static constexpr std::size_t kTrackCount = AudioEngine::kTrackCount;
    static constexpr std::size_t kSlotCount = InsertChain::kSlotCount;

    explicit MixerInsertEffectOwner(MixerController& controller) noexcept
        : controller_(controller) {}
    MixerInsertEffectOwner(const MixerInsertEffectOwner&) = delete;
    MixerInsertEffectOwner& operator=(const MixerInsertEffectOwner&) = delete;

    // Ownership transfers only on success. On failure candidate is unchanged.
    bool publish(std::size_t track, std::size_t slot,
                 std::unique_ptr<AudioInsert>& candidate,
                 std::uint32_t& ticket) noexcept {
        if (!validSlot(track, slot) || !candidate ||
            slots_[track][slot].pendingActive) return false;
        Slot& state = slots_[track][slot];
        if (!controller_.setTrackInsert(track, slot, candidate.get(), ticket))
            return false;
        state.pending = std::move(candidate);
        state.ticket = ticket;
        state.pendingActive = true;
        return true;
    }

    // Queue a null pointer while retaining the current object until ack.
    bool clear(std::size_t track, std::size_t slot,
               std::uint32_t& ticket) noexcept {
        if (!validSlot(track, slot) || slots_[track][slot].pendingActive)
            return false;
        Slot& state = slots_[track][slot];
        if (!controller_.setTrackInsert(track, slot, nullptr, ticket))
            return false;
        state.ticket = ticket;
        state.pendingActive = true;
        return true;
    }

    // Poll only on the serialized control thread. Retired objects are
    // destroyed here, never from the audio callback.
    std::size_t collectApplied() noexcept {
        std::size_t count = 0;
        for (auto& track : slots_) {
            for (auto& slot : track) {
                if (!slot.pendingActive ||
                    !controller_.isInsertUpdateApplied(slot.ticket)) continue;
                slot.current = std::move(slot.pending);
                slot.ticket = 0;
                slot.pendingActive = false;
                ++count;
            }
        }
        for (auto& side : audioInputSlots_) {
            for (auto& slot : side) {
                if (!slot.pendingActive ||
                    !controller_.isAudioInputPairInsertUpdateApplied(slot.ticket))
                    continue;
                slot.current = std::move(slot.pending);
                slot.ticket = 0;
                slot.pendingActive = false;
                ++count;
            }
        }
        return count;
    }

    bool hasPending(std::size_t track, std::size_t slot) const noexcept {
        return validSlot(track, slot) && slots_[track][slot].pendingActive;
    }
    AudioInsert* current(std::size_t track, std::size_t slot) const noexcept {
        return validSlot(track, slot) ? slots_[track][slot].current.get() : nullptr;
    }

    bool publishAudioInputPair(std::size_t side, std::size_t slot,
                               std::unique_ptr<AudioInsert>& candidate,
                               std::uint32_t& ticket) noexcept {
        if (!validPairSlot(side, slot) || !candidate ||
            audioInputSlots_[side][slot].pendingActive) return false;
        Slot& state = audioInputSlots_[side][slot];
        if (!controller_.setAudioInputPairInsert(side, slot, candidate.get(), ticket))
            return false;
        state.pending = std::move(candidate);
        state.ticket = ticket;
        state.pendingActive = true;
        return true;
    }

    bool clearAudioInputPair(std::size_t side, std::size_t slot,
                             std::uint32_t& ticket) noexcept {
        if (!validPairSlot(side, slot) ||
            audioInputSlots_[side][slot].pendingActive) return false;
        Slot& state = audioInputSlots_[side][slot];
        if (!controller_.setAudioInputPairInsert(side, slot, nullptr, ticket))
            return false;
        state.ticket = ticket;
        state.pendingActive = true;
        return true;
    }

    bool hasPendingAudioInputPair(std::size_t side, std::size_t slot) const noexcept {
        return validPairSlot(side, slot) &&
            audioInputSlots_[side][slot].pendingActive;
    }

    AudioInsert* currentAudioInputPair(std::size_t side,
                                       std::size_t slot) const noexcept {
        return validPairSlot(side, slot)
            ? audioInputSlots_[side][slot].current.get() : nullptr;
    }

private:
    struct Slot {
        std::unique_ptr<AudioInsert> current;
        std::unique_ptr<AudioInsert> pending;
        std::uint32_t ticket = 0;
        bool pendingActive = false;
    };
    static bool validSlot(std::size_t track, std::size_t slot) noexcept {
        return track < kTrackCount && slot < kSlotCount;
    }
    static bool validPairSlot(std::size_t side, std::size_t slot) noexcept {
        return side < 2 && slot < kSlotCount;
    }
    MixerController& controller_;
    std::array<std::array<Slot, kSlotCount>, kTrackCount> slots_{};
    std::array<std::array<Slot, kSlotCount>, 2> audioInputSlots_{};
};
} // namespace mpcmixer
