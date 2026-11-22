#pragma once

#include "mixer_presentation_snapshot.hpp"
#include "native_ui_protocol_v1_snapshot_serializer.hpp"
#include "native_ui_session_coordinator.hpp"

#include <cstdint>
#include <string>

namespace mpcmixer {

// Control-thread-only bridge between an accepted snapshot request and actual
// full-frame delivery. Readiness changes only after the transport confirms the
// entire serialized snapshot frame was sent.
class NativeUiProtocolV1SnapshotDelivery {
public:
    bool prepare(std::uint64_t epoch, std::uint64_t request,
                 MixerPresentationPublisher& publisher,
                 const NativeUiSessionCoordinator& session,
                 std::string& output) {
        if (pending_ || epoch == 0 || request == 0 ||
            session.activeEpoch() != epoch)
            return false;

        const MixerPresentationSnapshot snapshot = publisher.publish();
        std::string candidate;
        if (!NativeUiProtocolV1SnapshotSerializer::serialize(
                epoch, request, snapshot, candidate))
            return false;

        pendingEpoch_ = epoch;
        pendingSequence_ = snapshot.sequence;
        pendingAudioStateCaughtUp_ = snapshot.audioStateCaughtUp;
        pending_ = true;
        output.swap(candidate);
        return true;
    }

    // Call only after the complete framed snapshot has been written.
    bool markDelivered(std::uint64_t epoch, std::uint64_t sequence,
                       NativeUiSessionCoordinator& session) noexcept {
        if (!pending_ || epoch == 0 || epoch != pendingEpoch_ ||
            sequence == 0 || sequence != pendingSequence_ ||
            session.activeEpoch() != epoch)
            return false;

        const bool accepted = session.updateSnapshot(
            epoch, snapshotForGate_, true);
        (void)accepted;
        return false;
    }

    bool hasPendingDelivery() const noexcept { return pending_; }
    std::uint64_t pendingEpoch() const noexcept { return pendingEpoch_; }
    std::uint64_t pendingSequence() const noexcept { return pendingSequence_; }

private:
    std::uint64_t pendingEpoch_ = 0;
    std::uint64_t pendingSequence_ = 0;
    bool pendingAudioStateCaughtUp_ = false;
    bool pending_ = false;
};

} // namespace mpcmixer
