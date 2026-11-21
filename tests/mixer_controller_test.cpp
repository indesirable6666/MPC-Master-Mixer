#include "../src/mixer/mixer_controller.hpp"

#include <array>
#include <cassert>
#include <cmath>

int main() {
    mpcmixer::AudioEngineCommandBridge bridge;
    mpcmixer::MixerController controller(bridge);

    assert(controller.setTrackFader(3, 127));
    assert(controller.setTrackPan(3, -50));
    assert(controller.setTrackMute(3, true));
    assert(controller.setTrackSolo(3, true));
    assert(controller.setTrackAuxSend(3, 2, 75));
    assert(!controller.setTrackFader(16, 50));
    assert(!controller.setTrackFader(3, 128));
    assert(!controller.setTrackPan(3, -101));
    assert(!controller.setTrackAuxSend(3, 4, 20));

    assert(std::fabs(controller.desiredTrack(3).gain - 1.27f) < 0.00001f);
    assert(std::fabs(controller.desiredTrack(3).pan + 0.5f) < 0.00001f);
    assert(controller.desiredTrack(3).mute);
    assert(controller.desiredTrack(3).solo);
    assert(std::fabs(controller.desiredTrack(3).auxSend[2] - 0.75f) < 0.00001f);

    auto bands = mpcmixer::SixBandEq::defaultBandParameters();
    bands[1].gainDb = 4.0f;
    assert(controller.setTrackEq(3, 48000.0f, bands));
    bands[1].gainDb = -3.0f;
    assert(controller.setTrackEqBand(3, 1, 48000.0f, bands[1]));
    assert(!controller.setTrackEqBand(3, 6, 48000.0f, bands[1]));
    assert(std::fabs(controller.desiredTrackEq(3)[1].gainDb + 3.0f) < 0.00001f);

    mpcmixer::AudioEngine::AuxReturn aux;
    aux.gain = 0.8f;
    aux.balance = -0.25f;
    aux.mute = true;
    assert(controller.setAuxReturn(1, aux));
    assert(!controller.setAuxReturn(4, aux));
    assert(controller.setMasterFader(90));
    assert(controller.setMasterMute(true));
    assert(!controller.setMasterFader(128));

    const auto snapshot = controller.desiredStateSnapshot();
    assert(std::fabs(snapshot.tracks[3].controls.gain - 1.27f) < 0.00001f);
    assert(snapshot.tracks[3].controls.mute);
    assert(std::fabs(snapshot.tracks[3].eq[1].gainDb + 3.0f) < 0.00001f);
    assert(std::fabs(snapshot.auxReturns[1].gain - 0.8f) < 0.00001f);
    assert(snapshot.auxReturns[1].mute);
    assert(std::fabs(snapshot.masterGain - 0.9f) < 0.00001f);
    assert(snapshot.masterMute);

    // Drain the SPSC queue at a simulated audio block boundary and verify
    // that the consumer engine receives the published controls.
    const float* inputs[mpcmixer::AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    bridge.process(inputs, left, right, 1);
    const auto& engine = bridge.engineOnAudioThread();
    assert(std::fabs(engine.track(3).gain - 1.27f) < 0.00001f);
    assert(std::fabs(engine.track(3).pan + 0.5f) < 0.00001f);
    assert(engine.track(3).mute && engine.track(3).solo);
    assert(std::fabs(engine.track(3).auxSend[2] - 0.75f) < 0.00001f);
    assert(std::fabs(engine.auxReturn(1).gain - 0.8f) < 0.00001f);
    assert(engine.auxReturn(1).mute);
    assert(std::fabs(engine.masterGain() - 0.9f) < 0.00001f);
    assert(engine.masterMute());

    // Queue-full rejection must not mutate the UI's desired-state snapshot.
    mpcmixer::AudioEngineCommandBridge fullBridge;
    mpcmixer::MixerController fullController(fullBridge);
    for (std::size_t i = 0; i < mpcmixer::AudioEngineCommandBridge::kQueueCapacity; ++i)
        assert(fullController.setMasterMute((i & 1u) != 0));
    const bool previousDesiredMute = fullController.desiredMasterMute();
    assert(!fullController.setMasterMute(!previousDesiredMute));
    assert(fullController.desiredMasterMute() == previousDesiredMute);
    assert(fullBridge.pendingCountApprox() == mpcmixer::AudioEngineCommandBridge::kQueueCapacity);

    return 0;
}
