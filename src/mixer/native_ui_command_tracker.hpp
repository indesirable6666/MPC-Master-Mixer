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

    // Inspects the oldest completed acknowledgement without consuming it.
    bool peekApplied(
        const MixerController& controller,
        NativeUiAudioAck& acknowledgement) const noexcept {
        if (count_ == 0) return false;
        const NativeUiAudioAck& oldest = pending_[head_];
        if (!controller.isAudioCommandApplied(oldest.audioTicket)) return false;
        acknowledgement = oldest;
        return true;
    }

    // Consumes only the exact acknowledgement previously inspected by the
    // serialized control thread. This permits serialization before removal.
    bool consumeApplied(const NativeUiAudioAck& acknowledgement) noexcept {
        if (count_ == 0) return false;
        const NativeUiAudioAck& oldest = pending_[head_];
        if (oldest.sequence != acknowledgement.sequence ||
            oldest.audioTicket != acknowledgement.audioTicket ||
            oldest.connectionEpoch != acknowledgement.connectionEpoch)
            return false;
        head_ = (head_ + 1) % kCapacity;
        --count_;
        return true;
    }

    bool popApplied(
        const MixerController& controller,
        NativeUiAudioAck& acknowledgement) noexcept {
        NativeUiAudioAck candidate;
        if (!peekApplied(controller, candidate) || !consumeApplied(candidate))
            return false;
        acknowledgement = candidate;
        return true;
    }

    // Drains completed acknowledgements from obsolete connections without
    // exposing them to the active transport. FIFO is preserved: an unapplied
    // oldest ticket still blocks later acknowledgements until it is applied.
    bool popAppliedForEpoch(
        const MixerController& controller,
        std::uint64_t activeConnectionEpoch,
        NativeUiAudioAck& acknowledgement) noexcept {
        if (activeConnectionEpoch == 0) return false;

        NativeUiAudioAck candidate;
        while (popApplied(controller, candidate)) {
            if (candidate.connectionEpoch == activeConnectionEpoch) {
                acknowledgement = candidate;
                return true;
            }
        }
        return false;
    }

    std::size_t pendingCount() const noexcept { return count_; }

private:
    std::array<NativeUiAudioAck, kCapacity> pending_{};
    std::size_t head_ = 0;
    std::size_t count_ = 0;
};

} // namespace mpcmixer
