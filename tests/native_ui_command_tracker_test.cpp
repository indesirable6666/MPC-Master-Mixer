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

    assert(tracker.canTrackAudioCommand());
    const auto first = dispatcher.dispatch(command);
    assert(first.accepted() && first.audioTicket != 0);
    assert(tracker.recordAccepted(first));
    assert(tracker.pendingCount() == 1);
    assert(!tracker.popApplied(controller, ack));

    command.sequence = 2;
    command.type = mpcmixer::MixerUiCommandType::SetMasterMute;
    command.flag = true;
    const auto second = dispatcher.dispatch(command);
    assert(second.accepted() && second.audioTicket != 0);
    assert(tracker.recordAccepted(second));
    assert(tracker.pendingCount() == 2);
    assert(!tracker.popApplied(controller, ack));

    const float* inputs[mpcmixer::AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    bridge.process(inputs, left, right, 1);

    assert(tracker.popApplied(controller, ack));
    assert(ack.sequence == 1 && ack.audioTicket == first.audioTicket);
    assert(tracker.popApplied(controller, ack));
    assert(ack.sequence == 2 && ack.audioTicket == second.audioTicket);
    assert(!tracker.popApplied(controller, ack));
    assert(tracker.pendingCount() == 0);

    // Accepted no-op commands have ticket zero and are acknowledged
    // synchronously by the caller; they do not occupy the pending ledger.
    command.sequence = 3;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputPairLinked;
    command.flag = false; // already unlinked
    const auto noOp = dispatcher.dispatch(command);
    assert(noOp.accepted() && noOp.audioTicket == 0);
    assert(tracker.recordAccepted(noOp));
    assert(tracker.pendingCount() == 0);

    // A rejected dispatch result must never be recorded as an ACK candidate.
    mpcmixer::MixerUiCommandResult rejected;
    rejected.status = mpcmixer::MixerUiCommandStatus::QueueFull;
    rejected.sequence = 4;
    assert(!tracker.recordAccepted(rejected));
    assert(tracker.pendingCount() == 0);
}
