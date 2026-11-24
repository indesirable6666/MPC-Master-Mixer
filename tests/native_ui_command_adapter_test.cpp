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
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());
    message.hasFrequencyHz = false;
    message.hasQ = false;
    message.hasGainDb = false;

    // EQ-only fields must not be silently accepted on an unrelated command.
    message.sequence = 9;
    message.type = "SetTrackFader";
    message.hasValue = true;
    message.value = 64;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);

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
    assert(dispatcher.lastAcceptedSequence() == 8);

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
    assert(dispatcher.lastAcceptedSequence() == 8);

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
    message.sequence = 8;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::OutOfOrder);
    assert(dispatcher.lastAcceptedSequence() == 8);

    // The same unconsumed sequence can be retried with a valid command.
    message.sequence = 9;
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

    // Typed Audio In commands share the same serialized dispatcher as tracks.
    message = {};
    message.sequence = 11;
    message.type = "SetAudioInputPairLinked";
    message.hasFlag = true;
    message.flag = true;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message = {};
    message.sequence = 12;
    message.type = "SetAudioInputPairStereoGain";
    message.hasValue = true;
    message.value = 80;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message = {};
    message.sequence = 13;
    message.type = "SetAudioInputPairBalance";
    message.hasValue = true;
    message.value = -25;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message = {};
    message.sequence = 14;
    message.type = "SetAudioInputPairStereoMute";
    message.hasFlag = true;
    message.flag = true;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message = {};
    message.sequence = 15;
    message.type = "SetAudioInputChannelAuxSend";
    message.hasTrack = true;
    message.track = 1;
    message.hasIndex = true;
    message.index = 2;
    message.hasValue = true;
    message.value = 75;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message = {};
    message.sequence = 16;
    message.type = "SetAudioInputChannelSolo";
    message.hasTrack = true;
    message.track = 0;
    message.hasFlag = true;
    message.flag = true;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    // Mono pan is rejected while linked and does not consume its sequence.
    message = {};
    message.sequence = 17;
    message.type = "SetAudioInputChannelPan";
    message.hasTrack = true;
    message.track = 1;
    message.hasValue = true;
    message.value = -30;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 16);

    message = {};
    message.sequence = 17;
    message.type = "SetAudioInputPairLinked";
    message.hasFlag = true;
    message.flag = false;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message = {};
    message.sequence = 18;
    message.type = "SetAudioInputChannelPan";
    message.hasTrack = true;
    message.track = 1;
    message.hasValue = true;
    message.value = -30;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message = {};
    message.sequence = 19;
    message.type = "SetAudioInputChannelEqBand";
    message.hasTrack = true;
    message.track = 1;
    message.hasIndex = true;
    message.index = 5;
    message.hasFrequencyHz = true;
    message.frequencyHz = 10000.0f;
    message.hasQ = true;
    message.q = 0.8f;
    message.hasGainDb = true;
    message.gainDb = -2.0f;
    message.hasSampleRate = true;
    message.sampleRate = 48000.0f;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message = {};
    message.sequence = 20;
    message.type = "SetAudioInputChannelGain";
    message.hasTrack = true;
    message.track = 0;
    message.hasValue = true;
    message.value = 90;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message = {};
    message.sequence = 21;
    message.type = "SetAudioInputChannelMute";
    message.hasTrack = true;
    message.track = 0;
    message.hasFlag = true;
    message.flag = false;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    message = {};
    message.sequence = 22;
    message.type = "SetAudioInputChannelSolo";
    message.hasTrack = true;
    message.track = 1;
    message.hasFlag = true;
    message.flag = false;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).accepted());

    const auto audioInput = controller.desiredStateSnapshot();
    assert(!audioInput.audioInputLinked);
    assert(audioInput.audioInputChannels[0].gain == 0.9f);
    assert(audioInput.audioInputChannels[1].gain == 0.8f);
    assert(!audioInput.audioInputChannels[0].mute);
    assert(audioInput.audioInputChannels[1].mute);
    assert(audioInput.audioInputChannels[0].solo);
    assert(!audioInput.audioInputChannels[1].solo);
    assert(audioInput.audioInputChannels[0].auxSend[2] == 0.75f);
    assert(audioInput.audioInputChannels[1].auxSend[2] == 0.75f);
    assert(audioInput.audioInputChannels[1].pan == -0.3f);
    assert(audioInput.audioInputEq[1][5].frequencyHz == 10000.0f);
    assert(audioInput.audioInputEq[1][5].q == 0.8f);
    assert(audioInput.audioInputEq[1][5].gainDb == -2.0f);
    assert(dispatcher.lastAcceptedSequence() == 22);

    // Audio Input EQ requires an explicit sample rate; malformed side/bus
    // indices are rejected before they can consume a command sequence.
    message = {};
    message.sequence = 23;
    message.type = "SetAudioInputChannelEqBand";
    message.hasTrack = true;
    message.track = 0;
    message.hasIndex = true;
    message.index = 0;
    message.hasFrequencyHz = true;
    message.frequencyHz = 1000.0f;
    message.hasQ = true;
    message.q = 1.0f;
    message.hasGainDb = true;
    message.gainDb = 0.0f;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    message.hasSampleRate = true;
    message.sampleRate = 48000.0f;
    message.track = 2;
    assert(mpcmixer::NativeUiCommandAdapter::dispatch(message, dispatcher).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 22);

}
