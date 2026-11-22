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

    // Drains applied tickets in FIFO order. Only acknowledgements belonging
    // to the active connection are exposed; obsolete completions are dropped.
    bool popApplied(
        const MixerController& controller,
        NativeUiAudioAck& acknowledgement) noexcept {
        const std::uint64_t epoch = epochs_.activeEpoch();
        if (epoch != 0)
            return tracker_.popAppliedForEpoch(controller, epoch, acknowledgement);

        NativeUiAudioAck discarded;
        while (tracker_.popApplied(controller, discarded)) {}
        return false;
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
