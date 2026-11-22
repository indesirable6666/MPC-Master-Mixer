#include "native_ui_protocol_v1_snapshot_delivery.hpp"
#include "native_ui_protocol_v1_snapshot_decoder.hpp"

#include <cassert>
#include <cstdint>
#include <string>

using namespace mpcmixer;

int main() {
    AudioEngineCommandBridge bridge;
    MixerController controller(bridge);
    MixerPresentationPublisher publisher(controller);
    NativeUiSessionCoordinator session;
    NativeUiProtocolV1SnapshotDelivery delivery;

    std::uint64_t epoch = 0;
    assert(session.beginConnection(epoch));
    assert(epoch != 0);

    // A queued fader change makes the first full snapshot explicitly pending.
    assert(controller.setTrackFader(0, 61));
    std::string wire = "unchanged";
    assert(delivery.prepare(epoch, 1, publisher, session, wire));
    assert(delivery.hasPendingDelivery());
    const std::uint64_t firstSequence = delivery.pendingSequence();

    MixerPresentationSnapshot decoded;
    assert(NativeUiProtocolV1SnapshotDecoder::decode(
        wire, epoch, 1, decoded) == NativeUiProtocolV1SnapshotDecodeStatus::Ok);
    assert(!decoded.audioStateCaughtUp);
    assert(!session.isReady(epoch));

    // A queued but not-yet-sent snapshot cannot open the command gate.
    assert(!delivery.markDelivered(epoch + 1, firstSequence, session));
    assert(!delivery.markDelivered(epoch, firstSequence + 1, session));
    assert(session.activeEpoch() == epoch);
    assert(!session.isReady(epoch));

    // The transport confirms full-frame delivery; gate records this snapshot,
    // but remains closed because its audio watermark is not applied yet.
    assert(delivery.markDelivered(epoch, firstSequence, session));
    assert(!delivery.hasPendingDelivery());
    assert(!session.isReady(epoch));

    // Once the callback applies the queued command, a newer complete snapshot
    // can be prepared and its confirmed delivery opens the gate.
    const float* inputs[AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    bridge.process(inputs, left, right, 1);

    assert(delivery.prepare(epoch, 2, publisher, session, wire));
    const std::uint64_t secondSequence = delivery.pendingSequence();
    assert(secondSequence > firstSequence);
    assert(NativeUiProtocolV1SnapshotDecoder::decode(
        wire, epoch, 2, decoded) == NativeUiProtocolV1SnapshotDecodeStatus::Ok);
    assert(decoded.audioStateCaughtUp);
    assert(!session.isReady(epoch));
    assert(delivery.markDelivered(epoch, secondSequence, session));
    assert(session.isReady(epoch));

    // Disconnect invalidates the epoch; old delivery confirmations are stale.
    assert(session.invalidate(epoch));
    assert(!delivery.markDelivered(epoch, secondSequence, session));
    return 0;
}
