#include "../src/mixer/mixer_ui_command_dispatcher.hpp"
#include "../src/mixer/mixer_presentation_snapshot.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

int main() {
    mpcmixer::AudioEngineCommandBridge bridge;
    mpcmixer::MixerController controller(bridge);
    mpcmixer::MixerUiCommandDispatcher dispatcher(controller);

    mpcmixer::MixerUiCommand command;
    command.sequence = 1;
    command.type = mpcmixer::MixerUiCommandType::SetTrackFader;
    command.track = 3;
    command.value = 127;
    auto result = dispatcher.dispatch(command);
    assert(result.accepted());
    assert(result.sequence == 1);
    assert(result.audioTicket != 0);
    assert(!controller.isAudioCommandApplied(result.audioTicket));

    command.sequence = 2;
    command.type = mpcmixer::MixerUiCommandType::SetTrackPan;
    command.value = -45;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 3;
    command.type = mpcmixer::MixerUiCommandType::SetTrackMute;
    command.flag = true;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 4;
    command.type = mpcmixer::MixerUiCommandType::SetTrackSolo;
    command.flag = true;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 5;
    command.type = mpcmixer::MixerUiCommandType::SetTrackAuxSend;
    command.index = 2;
    command.value = 77;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 6;
    command.type = mpcmixer::MixerUiCommandType::SetTrackEqBand;
    command.index = 4;
    command.sampleRate = 48000.0f;
    command.eqBand = {4200.0f, 1.2f, -3.5f};
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 7;
    command.type = mpcmixer::MixerUiCommandType::SetAuxReturn;
    command.index = 1;
    command.value = 115;
    command.secondaryValue = 25;
    command.flag = true;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 8;
    command.type = mpcmixer::MixerUiCommandType::SetMasterFader;
    command.value = 123;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 9;
    command.type = mpcmixer::MixerUiCommandType::SetMasterMute;
    command.flag = true;
    assert(dispatcher.dispatch(command).accepted());
    command.sequence = 10;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputChannelPan;
    command.track = 1;
    command.value = 40;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 11;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputPairLinked;
    command.flag = true;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 12;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputPairStereoGain;
    command.value = 85;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 13;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputPairBalance;
    command.value = -30;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 14;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputPairStereoMute;
    command.flag = false;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 15;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputChannelGain;
    command.track = 0;
    command.value = 65;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 16;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputChannelMute;
    command.track = 1;
    command.flag = true;
    assert(dispatcher.dispatch(command).accepted());

    command.sequence = 17;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputChannelSolo;
    command.track = 1;
    command.flag = true;
    assert(dispatcher.dispatch(command).accepted());
    assert(dispatcher.lastAcceptedSequence() == 17);

    mpcmixer::MixerPresentationPublisher publisher(controller);
    const auto snapshot = publisher.publish();
    assert(snapshot.tracks[3].faderValue == 127);
    assert(snapshot.tracks[3].panPercent == -45);
    assert(snapshot.tracks[3].mute);
    assert(snapshot.tracks[3].solo);
    assert(snapshot.tracks[3].auxSendPercent[2] == 77);
    assert(std::fabs(snapshot.tracks[3].eq[4].frequencyHz - 4200.0f) < 0.001f);
    assert(std::fabs(snapshot.tracks[3].eq[4].gainDb + 3.5f) < 0.001f);
    assert(snapshot.auxReturns[1].levelValue == 115);
    assert(snapshot.auxReturns[1].balancePercent == 25);
    assert(snapshot.auxReturns[1].mute);
    assert(snapshot.masterFaderValue == 123);
    assert(snapshot.masterMute);
    assert(snapshot.audioInputPair.linked);
    assert(snapshot.audioInputPair.stereoGainValue == 65);
    assert(snapshot.audioInputPair.balancePercent == -30);
    assert(snapshot.audioInputPair.stereoMute);
    assert(snapshot.audioInputPair.stereoSolo);
    assert(snapshot.audioInputPair.channels[0].gainValue == 65);
    assert(snapshot.audioInputPair.channels[1].gainValue == 65);
    assert(snapshot.audioInputPair.channels[1].panPercent == 40);
    assert(snapshot.audioInputPair.channels[0].mute);
    assert(snapshot.audioInputPair.channels[1].mute);
    assert(snapshot.audioInputPair.channels[0].solo);
    assert(snapshot.audioInputPair.channels[1].solo);

    // Dispatcher acknowledgements refer to audio-thread application, not
    // merely acceptance into the producer queue.
    const float* appliedInputs[mpcmixer::AudioEngine::kTrackCount]{};
    float appliedLeft[1]{};
    float appliedRight[1]{};
    bridge.process(appliedInputs, appliedLeft, appliedRight, 1);
    assert(controller.isAudioCommandApplied(result.audioTicket));

    // Zero, repeated, and older sequence numbers are rejected.
    command.sequence = 0;
    command.type = mpcmixer::MixerUiCommandType::SetMasterFader;
    command.value = 10;
    assert(dispatcher.dispatch(command).status ==
           mpcmixer::MixerUiCommandStatus::OutOfOrder);
    assert(dispatcher.lastAcceptedSequence() == 17);

    // A repeated or older sequence is rejected without mutating state.
    command.sequence = 17;
    command.value = 10;
    command.type = mpcmixer::MixerUiCommandType::SetMasterFader;
    const auto replay = dispatcher.dispatch(command);
    assert(replay.status == mpcmixer::MixerUiCommandStatus::OutOfOrder);
    assert(controller.desiredMasterGain() == 1.23f);

    // Mono pan is disabled while linked and does not consume a sequence.
    command.sequence = 18;
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputChannelPan;
    command.track = 1;
    command.value = 10;
    assert(dispatcher.dispatch(command).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);

    // Invalid values and unsupported insert requests do not consume sequence.
    command.sequence = 18;
    command.type = static_cast<mpcmixer::MixerUiCommandType>(255);
    assert(dispatcher.dispatch(command).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    command.type = mpcmixer::MixerUiCommandType::SetTrackFader;
    command.track = 3;
    command.value = 128;
    assert(dispatcher.dispatch(command).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    command.type = mpcmixer::MixerUiCommandType::SetInsertEnabled;
    assert(dispatcher.dispatch(command).status ==
           mpcmixer::MixerUiCommandStatus::Unsupported);
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputChannelGain;
    command.track = 2;
    command.value = 50;
    assert(dispatcher.dispatch(command).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    command.type = mpcmixer::MixerUiCommandType::SetAudioInputPairBalance;
    command.value = 101;
    assert(dispatcher.dispatch(command).status ==
           mpcmixer::MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 17);

    // A full native command queue reports backpressure and permits retrying
    // the same sequence after the audio consumer drains the queue.
    mpcmixer::AudioEngineCommandBridge fullBridge;
    mpcmixer::MixerController fullController(fullBridge);
    mpcmixer::MixerUiCommandDispatcher fullDispatcher(fullController);
    for (std::size_t i = 0;
         i < mpcmixer::AudioEngineCommandBridge::kQueueCapacity; ++i) {
        assert(fullController.setMasterMute((i & 1u) != 0));
    }

    mpcmixer::MixerUiCommand retry;
    retry.sequence = 1;
    retry.type = mpcmixer::MixerUiCommandType::SetTrackFader;
    retry.track = 0;
    retry.value = 64;
    assert(fullDispatcher.dispatch(retry).status ==
           mpcmixer::MixerUiCommandStatus::QueueFull);
    assert(fullDispatcher.lastAcceptedSequence() == 0);
    assert(fullController.desiredTrack(0).gain == 1.0f);
    mpcmixer::MixerPresentationPublisher fullPublisher(fullController);
    const auto beforeDrain = fullPublisher.publish();
    assert(beforeDrain.tracks[0].faderValue == 100);

    const float* inputs[mpcmixer::AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    fullBridge.process(inputs, left, right, 1);
    assert(fullDispatcher.dispatch(retry).accepted());
    assert(fullDispatcher.lastAcceptedSequence() == 1);
    assert(fullController.desiredTrack(0).gain == 0.64f);
    const auto afterRetry = fullPublisher.publish();
    assert(afterRetry.tracks[0].faderValue == 64);

    // Gaps are allowed (transport sequence numbers need only increase).
    retry.sequence = 3;
    retry.value = 72;
    assert(fullDispatcher.dispatch(retry).accepted());
    assert(fullDispatcher.lastAcceptedSequence() == 3);
    retry.sequence = 2;
    retry.value = 80;
    assert(fullDispatcher.dispatch(retry).status ==
           mpcmixer::MixerUiCommandStatus::OutOfOrder);
    assert(fullController.desiredTrack(0).gain == 0.72f);
}
