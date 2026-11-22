#include "../src/mixer/mixer_presentation_snapshot.hpp"
#include "../src/mixer/mixer_insert_effect_owner.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <memory>
#include <utility>

int main() {
    mpcmixer::AudioEngineCommandBridge bridge;
    mpcmixer::MixerController controller(bridge);
    mpcmixer::MixerPresentationPublisher publisher(controller);
    mpcmixer::MixerInsertEffectOwner insertOwner(controller);

    const auto initial = publisher.publish();
    assert(initial.sequence == 1);
    assert(initial.masterFaderValue == 100);
    assert(!initial.masterMute);
    assert(initial.tracks[0].faderValue == 100);
    assert(initial.tracks[0].panPercent == 0);
    assert(!initial.tracks[0].mute);
    assert(!initial.tracks[0].solo);
    assert(!initial.tracks[0].hasInsert[0]);
    assert(!initial.audioInputPair.linked);
    assert(initial.audioInputPair.stereoGainValue == 100);
    assert(initial.audioInputPair.balancePercent == 0);
    assert(!initial.audioInputPair.stereoSolo);
    assert(initial.audioInputPair.channels[0].gainValue == 100);

    assert(controller.setTrackFader(5, 73));
    assert(controller.setTrackPan(5, -35));
    assert(controller.setTrackMute(5, true));
    assert(controller.setTrackSolo(5, true));
    assert(controller.setTrackAuxSend(5, 3, 42));

    auto bands = mpcmixer::SixBandEq::defaultBandParameters();
    bands[2].frequencyHz = 850.0f;
    bands[2].gainDb = 5.5f;
    assert(controller.setTrackEq(5, 48000.0f, bands));

    std::unique_ptr<mpcmixer::AudioInsert> insert =
        std::make_unique<mpcmixer::GainInsert>();
    std::uint32_t ticket = 0;
    assert(insertOwner.publish(5, 1, insert, ticket));

    mpcmixer::AudioEngine::AuxReturn aux;
    aux.gain = 0.64f;
    aux.balance = -0.3f;
    aux.mute = true;
    assert(controller.setAuxReturn(2, aux));
    assert(controller.setMasterFader(88));
    assert(controller.setMasterMute(true));
    assert(controller.setAudioInputPairChannelPan(0, -45));
    assert(controller.setAudioInputPairLinked(true));
    assert(controller.setAudioInputPairStereoGain(77));
    assert(controller.setAudioInputPairBalance(-35));
    assert(controller.setAudioInputPairStereoMute(true));
    assert(controller.setAudioInputPairChannelGain(0, 62));
    assert(!controller.setAudioInputPairChannelPan(0, -45));
    assert(controller.setAudioInputPairChannelMute(1, true));
    assert(controller.setAudioInputPairChannelSolo(1, true));

    const auto changed = publisher.publish();
    assert(changed.sequence == 2);
    assert(publisher.lastSequence() == 2);
    assert(changed.tracks[5].faderValue == 73);
    assert(changed.tracks[5].panPercent == -35);
    assert(changed.tracks[5].mute);
    assert(changed.tracks[5].solo);
    assert(changed.tracks[5].auxSendPercent[3] == 42);
    assert(std::fabs(changed.tracks[5].eq[2].frequencyHz - 850.0f) < 0.001f);
    assert(std::fabs(changed.tracks[5].eq[2].gainDb - 5.5f) < 0.001f);
    assert(changed.tracks[5].hasInsert[1]);
    assert(!changed.tracks[5].hasInsert[0]);
    assert(changed.auxReturns[2].levelValue == 64);
    assert(changed.auxReturns[2].balancePercent == -30);
    assert(changed.auxReturns[2].mute);
    assert(changed.masterFaderValue == 88);
    assert(changed.masterMute);
    assert(changed.audioInputPair.linked);
    assert(changed.audioInputPair.stereoGainValue == 62);
    assert(changed.audioInputPair.balancePercent == -35);
    assert(changed.audioInputPair.stereoMute);
    assert(changed.audioInputPair.stereoSolo);
    assert(changed.audioInputPair.channels[0].gainValue == 62);
    assert(changed.audioInputPair.channels[0].panPercent == -45);
    assert(changed.audioInputPair.channels[1].gainValue == 62);
    assert(changed.audioInputPair.channels[0].mute);
    assert(changed.audioInputPair.channels[1].mute);
    assert(changed.audioInputPair.channels[0].solo);
    assert(changed.audioInputPair.channels[1].solo);

    // Presentation is a copy: later edits do not mutate a previously
    // published frame, and the snapshot carries no insert object pointers.
    assert(controller.setTrackFader(5, 50));
    const float* inputs[mpcmixer::AudioEngine::kTrackCount]{};
    float outputLeft[1]{};
    float outputRight[1]{};
    bridge.process(inputs, outputLeft, outputRight, 1);
    assert(controller.isInsertUpdateApplied(ticket));
    assert(insertOwner.collectApplied() == 1);
    assert(insertOwner.clear(5, 1, ticket));
    bridge.process(inputs, outputLeft, outputRight, 1);
    assert(controller.isInsertUpdateApplied(ticket));
    assert(insertOwner.collectApplied() == 1);
    assert(changed.tracks[5].faderValue == 73);
    assert(changed.tracks[5].hasInsert[1]);

    const auto later = publisher.publish();
    assert(later.sequence == 3);
    assert(later.tracks[5].faderValue == 50);
    assert(!later.tracks[5].hasInsert[1]);

    // A failed enqueue must not alter desired state or the next presentation.
    mpcmixer::AudioEngineCommandBridge fullBridge;
    mpcmixer::MixerController fullController(fullBridge);
    mpcmixer::MixerPresentationPublisher fullPublisher(fullController);
    for (std::size_t i = 0; i < mpcmixer::AudioEngineCommandBridge::kQueueCapacity; ++i) {
        assert(fullController.setMasterMute((i & 1u) != 0));
    }
    const auto beforeRejectedEdit = fullPublisher.publish();
    assert(!fullController.setTrackFader(1, 65));
    assert(!fullController.setAudioInputPairLinked(true));
    assert(!fullController.desiredAudioInputPairLinked());
    const auto afterRejectedEdit = fullPublisher.publish();
    assert(afterRejectedEdit.sequence == beforeRejectedEdit.sequence + 1);
    assert(afterRejectedEdit.tracks[1].faderValue ==
           beforeRejectedEdit.tracks[1].faderValue);

    // Audio In presentation follows link/unlink transitions without losing
    // the independent mono pans. Linking seeds shared controls from L; edits
    // while linked mirror to both channels; unlinking restores two mono strips.
    mpcmixer::AudioEngineCommandBridge pairBridge;
    mpcmixer::MixerController pairController(pairBridge);
    mpcmixer::MixerPresentationPublisher pairPublisher(pairController);
    assert(pairController.setAudioInputPairChannelGain(0, 72));
    assert(pairController.setAudioInputPairChannelGain(1, 38));
    assert(pairController.setAudioInputPairChannelPan(0, -60));
    assert(pairController.setAudioInputPairChannelPan(1, 45));
    assert(pairController.setAudioInputPairChannelMute(0, true));
    assert(pairController.setAudioInputPairChannelSolo(1, true));
    assert(pairController.setAudioInputPairLinked(true));
    const auto linkedFrame = pairPublisher.publish();
    assert(linkedFrame.audioInputPair.linked);
    assert(linkedFrame.audioInputPair.stereoGainValue == 72);
    assert(linkedFrame.audioInputPair.stereoMute);
    assert(!linkedFrame.audioInputPair.stereoSolo);
    assert(linkedFrame.audioInputPair.channels[0].gainValue == 72);
    assert(linkedFrame.audioInputPair.channels[1].gainValue == 72);
    assert(linkedFrame.audioInputPair.channels[0].mute);
    assert(linkedFrame.audioInputPair.channels[1].mute);
    assert(!linkedFrame.audioInputPair.channels[0].solo);
    assert(!linkedFrame.audioInputPair.channels[1].solo);
    assert(linkedFrame.audioInputPair.channels[0].panPercent == -60);
    assert(linkedFrame.audioInputPair.channels[1].panPercent == 45);

    assert(pairController.setAudioInputPairStereoGain(91));
    assert(pairController.setAudioInputPairBalance(-25));
    assert(pairController.setAudioInputPairStereoMute(false));
    assert(pairController.setAudioInputPairChannelSolo(0, true));
    assert(pairController.setAudioInputPairLinked(false));
    const auto unlinkedFrame = pairPublisher.publish();
    assert(!unlinkedFrame.audioInputPair.linked);
    assert(unlinkedFrame.audioInputPair.stereoGainValue == 91);
    assert(unlinkedFrame.audioInputPair.balancePercent == -25);
    assert(unlinkedFrame.audioInputPair.channels[0].gainValue == 91);
    assert(unlinkedFrame.audioInputPair.channels[1].gainValue == 91);
    assert(!unlinkedFrame.audioInputPair.channels[0].mute);
    assert(!unlinkedFrame.audioInputPair.channels[1].mute);
    assert(unlinkedFrame.audioInputPair.channels[0].solo);
    assert(unlinkedFrame.audioInputPair.channels[1].solo);
    assert(unlinkedFrame.audioInputPair.channels[0].panPercent == -60);
    assert(unlinkedFrame.audioInputPair.channels[1].panPercent == 45);
    assert(!pairController.setAudioInputPairStereoGain(50));
    assert(pairController.setAudioInputPairChannelGain(1, 33));
    assert(pairController.setAudioInputPairChannelPan(1, 12));
    const auto independentFrame = pairPublisher.publish();
    assert(!independentFrame.audioInputPair.linked);
    assert(independentFrame.audioInputPair.channels[0].gainValue == 91);
    assert(independentFrame.audioInputPair.channels[1].gainValue == 33);
    assert(independentFrame.audioInputPair.channels[0].panPercent == -60);
    assert(independentFrame.audioInputPair.channels[1].panPercent == 12);
    // Audio In presentation exposes per-side EQ, Aux levels and insert
    // occupancy without leaking effect pointers into the UI snapshot.
    auto leftInputEq = mpcmixer::SixBandEq::defaultBandParameters();
    auto rightInputEq = mpcmixer::SixBandEq::defaultBandParameters();
    leftInputEq[2].gainDb = 2.0f;
    rightInputEq[2].gainDb = 4.0f;
    assert(pairController.setAudioInputPairEq(0, 48000.0f, leftInputEq));
    assert(pairController.setAudioInputPairEq(1, 44100.0f, rightInputEq));
    assert(pairController.setAudioInputPairAuxSend(0, 1, 0.25f));
    assert(pairController.setAudioInputPairAuxSend(1, 1, 0.75f));
    const auto unlinkedDspFrame = pairPublisher.publish();
    assert(unlinkedDspFrame.audioInputPair.channels[0].eqSampleRate == 48000.0f);
    assert(unlinkedDspFrame.audioInputPair.channels[1].eqSampleRate == 44100.0f);
    assert(unlinkedDspFrame.audioInputPair.channels[0].eq[2].gainDb == 2.0f);
    assert(unlinkedDspFrame.audioInputPair.channels[1].eq[2].gainDb == 4.0f);
    assert(unlinkedDspFrame.audioInputPair.channels[0].auxSendPercent[1] == 25);
    assert(unlinkedDspFrame.audioInputPair.channels[1].auxSendPercent[1] == 75);

    assert(pairController.setAudioInputPairLinked(true));
    const auto linkedDspFrame = pairPublisher.publish();
    assert(linkedDspFrame.audioInputPair.stereoAuxSendPercent[1] == 25);
    assert(linkedDspFrame.audioInputPair.channels[0].auxSendPercent[1] == 25);
    assert(linkedDspFrame.audioInputPair.channels[1].auxSendPercent[1] == 25);
    assert(linkedDspFrame.audioInputPair.channels[0].eq[2].gainDb == 2.0f);
    assert(linkedDspFrame.audioInputPair.channels[1].eq[2].gainDb == 2.0f);
    assert(linkedDspFrame.audioInputPair.channels[0].eqSampleRate == 48000.0f);
    assert(linkedDspFrame.audioInputPair.channels[1].eqSampleRate == 48000.0f);

}
