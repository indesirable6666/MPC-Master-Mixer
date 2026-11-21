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
    assert(dispatcher.lastAcceptedSequence() == 9);

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

    // A repeated or older sequence is rejected without mutating state.
    command.sequence = 9;
    command.value = 10;
    command.type = mpcmixer::MixerUiCommandType::SetMasterFader;
    const auto replay = dispatcher.dispatch(command);
    assert(replay.status == mpcmixer::MixerUiCommandStatus::OutOfOrder);
    assert(controller.desiredMasterGain() == 1.23f);

    // Invalid values and unsupported insert requests do not consume sequence.
    command.sequence = 10;
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
    assert(dispatcher.lastAcceptedSequence() == 9);

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

    const float* inputs[mpcmixer::AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    fullBridge.process(inputs, left, right, 1);
    assert(fullDispatcher.dispatch(retry).accepted());
    assert(fullDispatcher.lastAcceptedSequence() == 1);
}
