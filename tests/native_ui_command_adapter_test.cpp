#include "../src/mixer/native_ui_command_adapter.hpp"
#include "../src/mixer/mixer_presentation_snapshot.hpp"

#include <cassert>

int main() {
    mpcmixer::AudioEngineCommandBridge bridge;
    mpcmixer::MixerController controller(bridge);
    mpcmixer::MixerUiCommandDispatcher dispatcher(controller);
    mpcmixer::NativeUiCommandMessage message;

    message.sequence = 1;
    message.type = "SetTrackFader";
    message.hasTrack = true;
    message.track = 5;
    message.hasValue = true;
    message.value = 96;
    auto result = mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher);
    assert(result.accepted() && result.audioTicket != 0);

    message.sequence = 2;
    message.type = "SetTrackPan";
    message.value = -35;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 3;
    message.type = "SetTrackMute";
    message.hasFlag = true;
    message.flag = true;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 4;
    message.type = "SetTrackSolo";
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 5;
    message.type = "SetMasterFader";
    message.hasTrack = false;
    message.value = 110;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 6;
    message.type = "SetMasterMute";
    message.flag = true;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 7;
    message.type = "SetTrackAuxSend";
    message.hasIndex = true;
    message.index = 2;
    message.hasValue = true;
    message.value = 42;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    mpcmixer::MixerPresentationPublisher publisher(controller);
    auto snapshot = publisher.publish();
    assert(snapshot.tracks[5].faderValue == 96);
    assert(snapshot.tracks[5].panPercent == -35);
    assert(snapshot.tracks[5].mute && snapshot.tracks[5].solo);
    assert(snapshot.masterFaderValue == 110 && snapshot.masterMute);
    assert(snapshot.tracks[5].auxSendPercent[2] == 42);

    // Missing fields and out-of-range values must not consume the sequence.
    message.sequence = 8;
    message.type = "SetTrackAuxSend";
    message.hasIndex = false;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    message.hasIndex = true;
    message.index = mpcmixer::AudioEngine::kAuxBusCount;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    message.index = 0;
    message.value = 101;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 7);

    message.sequence = 8;
    message.type = "SetTrackFader";
    message.hasValue = false;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 6);

    message.hasValue = true;
    message.value = 128;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    message.value = 64;
    message.track = 16;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);

    message.type = "SetMasterMute";
    message.hasFlag = false;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    message.type = "SetInsertEnabled";
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 6);

    // Sequence checks happen in the dispatcher after the adapter has decoded
    // a valid typed command. Use a valid payload to isolate ordering behavior.
    message.type = "SetMasterMute";
    message.hasFlag = true;
    message.flag = false;
    message.sequence = 0;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::OutOfOrder);
    message.sequence = 6;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::OutOfOrder);
    assert(dispatcher.lastAcceptedSequence() == 6);

    // The same unconsumed sequence can be retried with a valid command.
    message.sequence = 7;
    message.type = "SetTrackFader";
    message.hasTrack = true;
    message.track = 5;
    message.hasValue = true;
    message.value = 0;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 9;
    message.value = mpcmixer::MixerController::kFaderMaximum;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    snapshot = publisher.publish();
    assert(snapshot.tracks[5].faderValue == mpcmixer::MixerController::kFaderMaximum);
    assert(dispatcher.lastAcceptedSequence() == 9);
}
