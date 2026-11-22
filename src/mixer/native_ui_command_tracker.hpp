#pragma once

#include "mixer_ui_command_dispatcher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>

namespace mpcmixer {

// Control-thread-only ledger for commands accepted by the dispatcher but not
// yet acknowledged by the audio callback. It owns no controller or transport.
struct NativeUiAudioAck {
    std::uint64_t sequence = 0;
    std::uint32_t audioTicket = 0;
    std::uint64_t connectionEpoch = 0;
};

// Fixed-capacity, allocation-free pending ledger. dispatch() reserves tracking
// capacity before invoking the dispatcher and retains every accepted nonzero
// audio ticket before returning. Commands with ticket zero are synchronous.
class NativeUiCommandTracker {
public:
    static constexpr std::size_t kCapacity =
        AudioEngineCommandBridge::kQueueCapacity;

    bool canTrackAudioCommand() const noexcept {
        return count_ < kCapacity;
    }

    // Atomically couples capacity reservation, dispatcher acceptance, and
    // ticket retention on the serialized control thread. QueueFull is returned
    // before dispatch, so the request sequence remains available for retry.
    MixerUiCommandResult dispatch(
        MixerUiCommandDispatcher& dispatcher,
        const MixerUiCommand& command,
        std::uint64_t connectionEpoch) noexcept {
        if (connectionEpoch == 0)
            return {MixerUiCommandStatus::Invalid, command.sequence};
        if (!canTrackAudioCommand())
            return {MixerUiCommandStatus::QueueFull, command.sequence};

        const MixerUiCommandResult result = dispatcher.dispatch(command);
        if (result.accepted() && result.audioTicket != 0) {
            const std::size_t tail = (head_ + count_) % kCapacity;
            pending_[tail] = {result.sequence, result.audioTicket, connectionEpoch};
            ++count_;
        }
        return result;
    }

    // Returns the oldest completed acknowledgement, preserving dispatch order.
    // The caller sends it to its transport and may call again to drain more.
    bool popApplied(
        const MixerController& controller,
        NativeUiAudioAck& acknowledgement) noexcept {
        if (count_ == 0) return false;
        const NativeUiAudioAck& oldest = pending_[head_];
        if (!controller.isAudioCommandApplied(oldest.audioTicket)) return false;

        acknowledgement = oldest;
        head_ = (head_ + 1) % kCapacity;
        --count_;
        return true;
    }

    std::size_t pendingCount() const noexcept { return count_; }

private:
    std::array<NativeUiAudioAck, kCapacity> pending_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

} // namespace mpcmixer
