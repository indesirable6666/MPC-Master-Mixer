#include "../src/mixer/native_ui_command_tracker.hpp"

#include <cassert>

int main() {
    mpcmixer::AudioEngineCommandBridge bridge;
    mpcmixer::MixerController controller(bridge);
    mpcmixer::MixerUiCommandDispatcher dispatcher(controller);
    mpcmixer::NativeUiCommandTracker tracker;
    mpcmixer::NativeUiAudioAck ack;

    mpcmixer::MixerUiCommand command;
    command.sequence = 1;
    command.type = mpcmixer::MixerUiCommandType::SetTrackFader;
    command.track = 0;
    command.value = 64;

    const auto missingEpoch = tracker.dispatch(dispatcher, command, 0);
    assert(missingEpoch.status == mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 0);

    const auto first = tracker.dispatch(dispatcher, command, 7);
    assert(first.accepted() && first.audioTicket != 0);
    assert(tracker.pendingCount() == 1);
    assert(!tracker.popApplied(controller, ack));

    command.sequence = 2;
    command.type = mpcmixer::MixerUiCommandType::SetMasterMute;
    command.flag = true;
    const auto second = tracker.dispatch(dispatcher, command, 7);
    assert(second.accepted() && second.audioTicket != 0);
    assert(tracker.pendingCount() == 2);
    assert(!tracker.popApplied(controller, ack));

    const float* inputs[mpcmixer::AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    bridge.process(inputs, left, right, 1);

    mpcmixer::NativeUiAudioAck inspected;
    assert(tracker.peekApplied(controller, inspected));
    assert(inspected.sequence == 1 && inspected.audioTicket == first.audioTicket &&
           inspected.connectionEpoch == 7);
    assert(tracker.pendingCount() == 2);
    auto mismatched = inspected;
    ++mismatched.sequence;
    assert(!tracker.consumeApplied(mismatched));
    assert(tracker.pendingCount() == 2);
    assert(tracker.consumeApplied(inspected));
    assert(tracker.pendingCount() == 1);
    assert(tracker.popApplied(controller, ack));
    assert(ack.sequence == 2 && ack.audioTicket == second.audioTicket && ack.connectionEpoch == 7);
    assert(!tracker.popApplied(controller, ack));
    assert(ack.sequence == 2 && ack.audioTicket == second.audioTicket && ack.connectionEpoch == 7);
    assert(!tracker.popApplied(controller, ack));
    assert(tracker.pendingCount() == 0);

    // Accepted no-op commands have ticket zero and are acknowledged
    // synchronously by the caller; they do not occupy the pending ledger.
    command.sequence = 3;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputPairLinked;
    command.flag = false; // already unlinked
    const auto noOp = tracker.dispatch(dispatcher, command, 7);
    assert(noOp.accepted() && noOp.audioTicket == 0);
    assert(tracker.pendingCount() == 0);

    // Fill the ledger while the callback is stopped. The next request must
    // be rejected before dispatcher sequence consumption, then be retryable
    // after acknowledgements are drained.
    for (std::size_t i = 0; i < mpcmixer::NativeUiCommandTracker::kCapacity; ++i) {
        command.sequence = 10 + i;
        command.type = mpcmixer::MixerUiCommandType::SetTrackFader;
        command.track = 0;
        command.value = static_cast<int>(i % 128);
        const auto accepted = tracker.dispatch(dispatcher, command, 7);
        assert(accepted.accepted() && accepted.audioTicket != 0);
    }
    assert(tracker.pendingCount() == mpcmixer::NativeUiCommandTracker::kCapacity);
    command.sequence = 10 + mpcmixer::NativeUiCommandTracker::kCapacity;
    const auto backpressured = tracker.dispatch(dispatcher, command, 7);
    assert(backpressured.status == mpcmixer::MixerUiCommandStatus::QueueFull);
    assert(dispatcher.lastAcceptedSequence() == command.sequence - 1);

    bridge.process(inputs, left, right, 1);
    std::size_t drained = 0;
    while (tracker.popApplied(controller, ack)) ++drained;
    assert(drained == mpcmixer::NativeUiCommandTracker::kCapacity);
    assert(tracker.pendingCount() == 0);

    const auto retried = tracker.dispatch(dispatcher, command, 7);
    assert(retried.accepted() && retried.audioTicket != 0);
    assert(tracker.pendingCount() == 1);
    bridge.process(inputs, left, right, 1);
    assert(tracker.popApplied(controller, ack));
    assert(ack.sequence == command.sequence && ack.connectionEpoch == 7);
    assert(!tracker.popApplied(controller, ack));

    // Reconnect with a new epoch while an old connection's command is still
    // pending. Once both tickets apply, the epoch-aware drain discards the
    // stale acknowledgement and returns only the new connection's result.
    command.sequence += 1;
    command.type = mpcmixer::MixerUiCommandType::SetTrackFader;
    command.track = 1;
    command.value = 80;
    const auto stale = tracker.dispatch(dispatcher, command, 7);
    assert(stale.accepted() && stale.audioTicket != 0);

    command.sequence += 1;
    command.value = 96;
    const auto current = tracker.dispatch(dispatcher, command, 8);
    assert(current.accepted() && current.audioTicket != 0);
    assert(tracker.pendingCount() == 2);

    bridge.process(inputs, left, right, 1);
    assert(tracker.popAppliedForEpoch(controller, 8, ack));
    assert(ack.sequence == command.sequence);
    assert(ack.audioTicket == current.audioTicket);
    assert(ack.connectionEpoch == 8);
    assert(tracker.pendingCount() == 0);
    assert(!tracker.popAppliedForEpoch(controller, 8, ack));
    assert(!tracker.popAppliedForEpoch(controller, 0, ack));
}
