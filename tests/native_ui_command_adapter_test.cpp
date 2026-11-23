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
    message.hasValue = false;
    message.hasFlag = true;
    message.flag = true;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 4;
    message.type = "SetTrackSolo";
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 5;
    message.type = "SetMasterFader";
    message.hasFlag = false;
    message.hasTrack = false;
    message.hasValue = true;
    message.value = 110;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 6;
    message.type = "SetMasterMute";
    message.hasTrack = false;
    message.hasIndex = false;
    message.hasValue = false;
    message.hasFlag = true;
    message.flag = true;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 7;
    message.type = "SetTrackAuxSend";
    message.hasTrack = true;
    message.track = 5;
    message.hasFlag = false;
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

    message.sequence = 8;
    message.type = "SetTrackEqBand";
    message.hasValue = false;
    message.hasFlag = false;
    message.hasIndex = true;
    message.index = 2;
    message.hasFrequencyHz = true; message.frequencyHz = 1200.0f;
    message.hasQ = true; message.q = 0.8f;
    message.hasGainDb = true; message.gainDb = 3.0f;
    assert(NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    // Missing fields and out-of-range values must not consume the sequence.
    message.sequence = 9;
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
    assert(dispatcher.lastAcceptedSequence() == 8);

    message.sequence = 9;
    message.type = "SetTrackFader";
    message.hasIndex = false;
    message.hasFlag = false;
    message.hasValue = false;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 7);

    message.hasValue = true;
    message.value = 128;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    message.value = 64;
    message.track = 16;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);

    message.type = "SetMasterMute";
    message.hasTrack = false;
    message.hasIndex = false;
    message.hasValue = false;
    message.hasFlag = false;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    message.type = "SetInsertEnabled";
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 7);

    // Sequence checks happen in the dispatcher after the adapter has decoded
    // a valid typed command. Use a valid payload to isolate ordering behavior.
    message.type = "SetMasterMute";
    message.hasTrack = false;
    message.hasIndex = false;
    message.hasValue = false;
    message.hasFlag = true;
    message.flag = false;
    message.sequence = 0;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::OutOfOrder);
    message.sequence = 7;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::OutOfOrder);
    assert(dispatcher.lastAcceptedSequence() == 7);

    // The same unconsumed sequence can be retried with a valid command.
    message.sequence = 8;
    message.type = "SetTrackFader";
    message.hasTrack = true;
    message.track = 5;
    message.hasIndex = false;
    message.hasFlag = false;
    message.hasValue = true;
    message.value = 0;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message.sequence = 10;
    message.value = mpcmixer::MixerController::kFaderMaximum;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    snapshot = publisher.publish();
    assert(snapshot.tracks[5].faderValue == mpcmixer::MixerController::kFaderMaximum);
    assert(dispatcher.lastAcceptedSequence() == 10);
}
