#include "../src/mixer/mixer_controller.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 1.0e-5f; }
}

int main() {
    using Engine = mpcmixer::AudioEngine;
    using Bridge = mpcmixer::AudioEngineCommandBridge;

    Bridge bridge;
    mpcmixer::MixerController controller(bridge);

    // Interleave rapid Mute/Solo changes on two strips. Every accepted
    // setter enqueues a complete snapshot, and the final snapshot for each
    // strip must retain the other control's latest value.
    assert(controller.setTrackMute(2, true));
    assert(controller.setTrackSolo(7, true));
    assert(controller.setTrackMute(2, false));
    assert(controller.setTrackSolo(2, true));
    assert(controller.setTrackMute(7, true));
    assert(controller.setTrackSolo(7, false));
    assert(controller.setTrackMute(2, true));
    assert(controller.setTrackSolo(2, false));

    assert(bridge.pendingCountApprox() == 8);

    const auto desired = controller.desiredStateSnapshot();
    assert(desired.tracks[2].controls.mute);
    assert(!desired.tracks[2].controls.solo);
    assert(desired.tracks[7].controls.mute);
    assert(!desired.tracks[7].controls.solo);

    // The audio bridge consumes the queued snapshots in FIFO order at the
    // next block boundary. Verify applied engine state independently from
    // the controller's desired-state copy.
    const float* inputs[Engine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    bridge.process(inputs, left, right, 1);
    assert(bridge.pendingCountApprox() == 0);

    const auto& engine = bridge.engineOnAudioThread();
    assert(engine.track(2).mute);
    assert(!engine.track(2).solo);
    assert(engine.track(7).mute);
    assert(!engine.track(7).solo);

    // A subsequent Solo-only update must not clear Mute, and a Mute-only
    // update must not clear Solo. Check the two directions independently.
    assert(controller.setTrackSolo(2, true));
    assert(controller.setTrackMute(7, false));
    bridge.process(inputs, left, right, 1);
    assert(engine.track(2).mute && engine.track(2).solo);
    assert(!engine.track(7).mute && !engine.track(7).solo);

    // Track controls remain independent; the Master mute is a separate
    // command and must not overwrite either strip.
    assert(controller.setMasterMute(true));
    bridge.process(inputs, left, right, 1);
    assert(engine.masterMute());
    assert(engine.track(2).mute && engine.track(2).solo);
    assert(!engine.track(7).mute && !engine.track(7).solo);

    return 0;
}
