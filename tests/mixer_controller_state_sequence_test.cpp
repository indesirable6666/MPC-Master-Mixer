#include "../src/mixer/mixer_controller.hpp"
#include "../src/mixer/mixer_presentation_snapshot.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 1.0e-5f; }
}

int main() {
    using Engine = mpcmixer::AudioEngine;
    using Bridge = mpcmixer::AudioEngineCommandBridge;

    // Rapid successive edits to the same strip must preserve every field:
    // each setter publishes a full track snapshot, not a partial overwrite.
    Bridge bridge;
    mpcmixer::MixerController controller(bridge);
    assert(controller.setTrackFader(5, 80));
    assert(controller.setTrackPan(5, -35));
    assert(controller.setTrackMute(5, true));
    assert(controller.setTrackSolo(5, true));
    assert(controller.setTrackAuxSend(5, 0, 20));
    assert(controller.setTrackAuxSend(5, 1, 40));
    assert(controller.setTrackAuxSend(5, 2, 60));
    assert(controller.setTrackAuxSend(5, 3, 80));

    // Interleave edits on another strip; neither strip may inherit the
    // other's controls or lose its own prior updates.
    assert(controller.setTrackFader(6, 45));
    assert(controller.setTrackPan(6, 70));
    assert(controller.setTrackMute(6, false));
    assert(controller.setTrackSolo(6, false));
    assert(controller.setTrackAuxSend(6, 3, 15));

    const auto desired = controller.desiredStateSnapshot();
    const auto& a = desired.tracks[5].controls;
    const auto& b = desired.tracks[6].controls;
    assert(near(a.gain, 0.80f));
    assert(near(a.pan, -0.35f));
    assert(a.mute && a.solo);
    assert(near(a.auxSend[0], 0.20f));
    assert(near(a.auxSend[1], 0.40f));
    assert(near(a.auxSend[2], 0.60f));
    assert(near(a.auxSend[3], 0.80f));
    assert(near(b.gain, 0.45f));
    assert(near(b.pan, 0.70f));
    assert(!b.mute && !b.solo);
    assert(near(b.auxSend[3], 0.15f));

    // Presentation is derived from the same desired-state snapshot and must
    // reflect the final value of every rapid edit.
    mpcmixer::MixerPresentationPublisher publisher(controller);
    const auto presentation = publisher.publish();
    assert(presentation.tracks[5].faderValue == 80);
    assert(presentation.tracks[5].panPercent == -35);
    assert(presentation.tracks[5].mute && presentation.tracks[5].solo);
    assert(presentation.tracks[5].auxSendPercent[0] == 20);
    assert(presentation.tracks[5].auxSendPercent[1] == 40);
    assert(presentation.tracks[5].auxSendPercent[2] == 60);
    assert(presentation.tracks[5].auxSendPercent[3] == 80);
    assert(presentation.tracks[6].faderValue == 45);
    assert(presentation.tracks[6].panPercent == 70);
    assert(!presentation.tracks[6].mute && !presentation.tracks[6].solo);
    assert(presentation.tracks[6].auxSendPercent[3] == 15);

    // All queued snapshots are applied in order at the next audio block.
    const float* inputs[Engine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    assert(bridge.pendingCountApprox() == 13);
    bridge.process(inputs, left, right, 1);
    assert(bridge.pendingCountApprox() == 0);
    const auto& engine = bridge.engineOnAudioThread();
    assert(near(engine.track(5).gain, 0.80f));
    assert(near(engine.track(5).pan, -0.35f));
    assert(engine.track(5).mute && engine.track(5).solo);
    assert(near(engine.track(5).auxSend[0], 0.20f));
    assert(near(engine.track(5).auxSend[3], 0.80f));
    assert(near(engine.track(6).gain, 0.45f));
    assert(near(engine.track(6).pan, 0.70f));
    assert(near(engine.track(6).auxSend[3], 0.15f));

    // Queue-full rejection must be transactional for each control family:
    // failed commands do not advance desired state or the presentation.
    Bridge fullBridge;
    mpcmixer::MixerController fullController(fullBridge);
    for (std::size_t i = 0; i < Bridge::kQueueCapacity; ++i)
        assert(fullBridge.enqueueMasterMute((i & 1u) != 0));

    const auto before = fullController.desiredStateSnapshot();
    assert(!fullController.setTrackFader(2, 75));
    assert(!fullController.setTrackPan(2, -50));
    assert(!fullController.setTrackMute(2, true));
    assert(!fullController.setTrackSolo(2, true));
    assert(!fullController.setTrackAuxSend(2, 3, 90));
    const auto after = fullController.desiredStateSnapshot();
    assert(near(after.tracks[2].controls.gain, before.tracks[2].controls.gain));
    assert(near(after.tracks[2].controls.pan, before.tracks[2].controls.pan));
    assert(after.tracks[2].controls.mute == before.tracks[2].controls.mute);
    assert(after.tracks[2].controls.solo == before.tracks[2].controls.solo);
    for (std::size_t bus = 0; bus < Engine::kAuxBusCount; ++bus)
        assert(near(after.tracks[2].controls.auxSend[bus],
                    before.tracks[2].controls.auxSend[bus]));

    mpcmixer::MixerPresentationPublisher fullPublisher(fullController);
    const auto fullPresentation = fullPublisher.publish();
    assert(fullPresentation.tracks[2].faderValue == 100);
    assert(fullPresentation.tracks[2].panPercent == 0);
    assert(!fullPresentation.tracks[2].mute && !fullPresentation.tracks[2].solo);
    for (std::size_t bus = 0; bus < Engine::kAuxBusCount; ++bus)
        assert(fullPresentation.tracks[2].auxSendPercent[bus] == 0);
    assert(fullBridge.pendingCountApprox() == Bridge::kQueueCapacity);

    // Invalid ranges are rejected without consuming queue capacity.
    assert(!fullController.setTrackFader(2, 128));
    assert(!fullController.setTrackPan(2, 101));
    assert(!fullController.setTrackAuxSend(2, 0, -1));
    assert(fullBridge.pendingCountApprox() == Bridge::kQueueCapacity);

    return 0;
}
