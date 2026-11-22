#include "../src/mixer/native_ui_session_coordinator.hpp"

#include <cassert>
#include <cstdint>

int main() {
    mpcmixer::AudioEngineCommandBridge bridge;
    mpcmixer::MixerController controller(bridge);
    mpcmixer::MixerUiCommandDispatcher dispatcher(controller);
    mpcmixer::MixerPresentationPublisher publisher(controller);
    mpcmixer::NativeUiSessionCoordinator session;
    mpcmixer::NativeUiAudioAck ack;

    std::uint64_t epoch = 99;
    mpcmixer::MixerUiCommand command;
    command.sequence = 1;
    command.type = mpcmixer::MixerUiCommandType::SetTrackFader;
    command.track = 0;
    command.value = 72;

    // No connection or readiness means no dispatch and no sequence consumption.
    auto result = session.dispatch(epoch, dispatcher, command);
    assert(result.status == mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 0);

    assert(session.beginConnection(epoch));
    assert(epoch == 1);
    assert(session.activeEpoch() == epoch);
    assert(!session.isReady(epoch));

    // Seed a pending audio command so the first complete snapshot is valid
    // but not yet caught up to its watermark.
    assert(controller.setTrackFader(1, 60));

    // A partial snapshot and a snapshot whose audio watermark is pending keep
    // the command path closed.
    auto snapshot = publisher.publish();
    assert(!snapshot.audioStateCaughtUp);
    assert(!session.updateSnapshot(epoch, snapshot, false));
    assert(session.updateSnapshot(epoch, snapshot, true));
    assert(!session.isReady(epoch));
    result = session.dispatch(epoch, dispatcher, command);
    assert(result.status == mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 0);

    // A fresh complete snapshot after the audio queue catches up unlocks it.
    const float* inputs[mpcmixer::AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    bridge.process(inputs, left, right, 1);
    snapshot = publisher.publish();
    assert(snapshot.audioStateCaughtUp);
    assert(session.updateSnapshot(epoch, snapshot, true));
    assert(session.isReady(epoch));

    result = session.dispatch(epoch, dispatcher, command);
    assert(result.accepted() && result.audioTicket != 0);
    assert(session.pendingAcknowledgements() == 1);
    assert(!session.popApplied(controller, ack));

    bridge.process(inputs, left, right, 1);
    assert(session.popApplied(controller, ack));
    assert(ack.sequence == 1);
    assert(ack.connectionEpoch == epoch);
    assert(ack.audioTicket == result.audioTicket);
    assert(!session.popApplied(controller, ack));

    // Disconnect closes command flow; stale epochs cannot dispatch or close
    // a later session.
    assert(session.invalidate(epoch));
    assert(!session.isReady(epoch));
    command.sequence = 2;
    result = session.dispatch(epoch, dispatcher, command);
    assert(result.status == mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 1);

    std::uint64_t nextEpoch = 0;
    assert(session.beginConnection(nextEpoch));
    assert(nextEpoch == 2);
    assert(!session.updateSnapshot(epoch, snapshot, true));
    assert(!session.isReady(nextEpoch));

    snapshot = publisher.publish();
    assert(session.updateSnapshot(nextEpoch, snapshot, true));
    assert(session.isReady(nextEpoch));
    command.sequence = 2;
    result = session.dispatch(nextEpoch, dispatcher, command);
    assert(result.accepted());
    assert(result.audioTicket != 0);

    // Leave epoch 2's accepted command outstanding across disconnect. The
    // tracker must retain its ticket, but never expose its ACK to epoch 3.
    const std::uint32_t obsoleteTicket = result.audioTicket;
    assert(session.pendingAcknowledgements() == 1);
    assert(session.invalidate(nextEpoch));

    std::uint64_t thirdEpoch = 0;
    assert(session.beginConnection(thirdEpoch));
    assert(thirdEpoch == 3);
    assert(!session.updateSnapshot(nextEpoch, snapshot, true));
    command.sequence = 3;
    result = session.dispatch(thirdEpoch, dispatcher, command);
    assert(result.status == mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 2);

    // The old ticket is still pending, so epoch 3 cannot become ready from a
    // snapshot whose audio watermark has not caught up.
    snapshot = publisher.publish();
    assert(!snapshot.audioStateCaughtUp);
    assert(session.updateSnapshot(thirdEpoch, snapshot, true));
    assert(!session.isReady(thirdEpoch));
    assert(!session.popApplied(controller, ack));

    // Once the callback applies the obsolete ticket, a newer full snapshot
    // opens epoch 3. The next command is queued behind the old completion.
    bridge.process(inputs, left, right, 1);
    snapshot = publisher.publish();
    assert(snapshot.audioStateCaughtUp);
    assert(session.updateSnapshot(thirdEpoch, snapshot, true));
    assert(session.isReady(thirdEpoch));

    command.sequence = 3;
    result = session.dispatch(thirdEpoch, dispatcher, command);
    assert(result.accepted() && result.audioTicket != 0);
    assert(result.audioTicket != obsoleteTicket);
    assert(session.pendingAcknowledgements() == 2);

    // A stale disconnect cannot invalidate epoch 3. When both tickets have
    // completed, FIFO draining discards epoch 2's ACK and exposes only epoch 3.
    assert(!session.invalidate(nextEpoch));
    assert(session.isReady(thirdEpoch));
    bridge.process(inputs, left, right, 1);
    assert(session.popApplied(controller, ack));
    assert(ack.sequence == 3);
    assert(ack.connectionEpoch == thirdEpoch);
    assert(ack.audioTicket == result.audioTicket);
    assert(!session.popApplied(controller, ack));
    assert(session.pendingAcknowledgements() == 0);

    assert(session.invalidate(thirdEpoch));
    assert(session.activeEpoch() == 0);
    return 0;
}
