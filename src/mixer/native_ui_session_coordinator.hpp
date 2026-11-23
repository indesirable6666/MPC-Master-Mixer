#pragma once

#include "mixer_presentation_snapshot.hpp"
#include "native_ui_command_tracker.hpp"
#include "native_ui_connection_epoch.hpp"
#include "native_ui_session_gate.hpp"

#include <cstdint>

namespace mpcmixer {

// Control-thread-only composition boundary for the transport-neutral native
// UI pieces. It owns sequencing/readiness/tracking, but no controller,
// dispatcher, socket, UI, or device resources.
class NativeUiSessionCoordinator {
public:
    bool beginConnection(std::uint64_t& epoch) noexcept {
        if (!epochs_.beginConnection(epoch)) return false;
        if (gate_.begin(epoch)) return true;
        epochs_.invalidate(epoch);
        epoch = 0;
        return false;
    }

    bool invalidate(std::uint64_t epoch) noexcept {
        if (!epochs_.isCurrent(epoch) || gate_.activeEpoch() != epoch)
            return false;
        const bool gateClosed = gate_.invalidate(epoch);
        const bool epochClosed = epochs_.invalidate(epoch);
        return gateClosed && epochClosed;
    }

    bool updateSnapshot(
        std::uint64_t epoch,
        const MixerPresentationSnapshot& snapshot,
        bool fullSnapshotReceived) noexcept {
        if (!epochs_.isCurrent(epoch)) return false;
        return gate_.updateSnapshot(
            epoch, snapshot.sequence, fullSnapshotReceived,
            snapshot.audioStateCaughtUp);
    }

    MixerUiCommandResult dispatch(
        std::uint64_t epoch,
        MixerUiCommandDispatcher& dispatcher,
        const MixerUiCommand& command) noexcept {
        if (!epochs_.isCurrent(epoch) || !gate_.acceptsCommands(epoch))
            return {MixerUiCommandStatus::Invalid, command.sequence};
        return tracker_.dispatch(dispatcher, command, epoch);
    }

    // Inspects the next active-epoch acknowledgement without consuming it.
    // Completed entries from obsolete epochs are discarded in FIFO order.
    bool peekApplied(
        const MixerController& controller,
        NativeUiAudioAck& acknowledgement) noexcept {
        const std::uint64_t epoch = epochs_.activeEpoch();
        if (epoch == 0) {
            NativeUiAudioAck discarded;
            while (tracker_.popApplied(controller, discarded)) {}
            return false;
        }

        NativeUiAudioAck candidate;
        while (tracker_.peekApplied(controller, candidate)) {
            if (candidate.connectionEpoch == epoch) {
                acknowledgement = candidate;
                return true;
            }
            if (!tracker_.consumeApplied(candidate)) return false;
        }
        return false;
    }

    // Removes the exact acknowledgement after its response was serialized.
    bool consumeApplied(const NativeUiAudioAck& acknowledgement) noexcept {
        if (acknowledgement.connectionEpoch == 0 ||
            acknowledgement.connectionEpoch != epochs_.activeEpoch())
            return false;
        return tracker_.consumeApplied(acknowledgement);
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

    bool isReady(std::uint64_t epoch) const noexcept {
        return epochs_.isCurrent(epoch) && gate_.acceptsCommands(epoch);
    }

    std::uint64_t activeEpoch() const noexcept {
        return epochs_.activeEpoch();
    }

    std::size_t pendingAcknowledgements() const noexcept {
        return tracker_.pendingCount();
    }

private:
    NativeUiConnectionEpoch epochs_;
    NativeUiSessionGate gate_;
    NativeUiCommandTracker tracker_;
};

} // namespace mpcmixer
