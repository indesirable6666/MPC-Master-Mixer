#include "../src/mixer/mixer_controller.hpp"
#include "../src/mixer/mixer_insert_effect_owner.hpp"

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

    // Stereo Audio In link/unlink transitions preserve the visible strip state.
    mpcmixer::AudioEngineCommandBridge pairBridge;
    mpcmixer::MixerController pairController(pairBridge);
    assert(pairController.setAudioInputPairChannelGain(0, 60));
    assert(pairController.setAudioInputPairChannelPan(1, 40));
    assert(pairController.setAudioInputPairLinked(true));
    assert(std::fabs(pairController.desiredAudioInputPairStereo().gain - 0.6f) < 0.00001f);
    assert(std::fabs(pairController.desiredAudioInputPairChannel(1).gain - 0.6f) < 0.00001f);
    assert(std::fabs(pairController.desiredAudioInputPairChannel(1).pan - 0.4f) < 0.00001f);
    assert(pairController.setAudioInputPairStereoGain(50));
    assert(pairController.setAudioInputPairBalance(25));
    assert(pairController.setAudioInputPairStereoMute(false));
    assert(pairController.setAudioInputPairChannelGain(0, 60));
    assert(!pairController.setAudioInputPairChannelPan(1, 40));
    assert(pairController.setAudioInputPairChannelMute(1, false));
    assert(pairController.setAudioInputPairChannelSolo(1, true));
    assert(!pairController.setAudioInputPairStereoGain(128));
    assert(!pairController.setAudioInputPairBalance(101));
    assert(!pairController.setAudioInputPairChannelGain(2, 50));
    assert(!pairController.setAudioInputPairChannelPan(0, -101));
    const auto pairDesired = pairController.desiredStateSnapshot();
    assert(pairDesired.audioInputLinked);
    assert(std::fabs(pairDesired.audioInputStereo.gain - 0.6f) < 0.00001f);
    assert(std::fabs(pairDesired.audioInputStereo.balance - 0.25f) < 0.00001f);
    assert(!pairDesired.audioInputStereo.mute);
    assert(pairDesired.audioInputStereo.solo);
    assert(std::fabs(pairDesired.audioInputChannels[1].gain - 0.6f) < 0.00001f);
    assert(std::fabs(pairDesired.audioInputChannels[1].pan - 0.4f) < 0.00001f);
    assert(!pairDesired.audioInputChannels[1].mute);
    assert(pairDesired.audioInputChannels[0].solo && pairDesired.audioInputChannels[1].solo);

    const float* noTrackInputs[mpcmixer::AudioEngine::kTrackCount]{};
    const float pairInputLeft[1] = {1.0f};
    const float pairInputRight[1] = {-0.5f};
    float pairOutputLeft[1]{};
    float pairOutputRight[1]{};
    pairBridge.processWithAudioInputPair(
        noTrackInputs, pairInputLeft, pairInputRight,
        pairOutputLeft, pairOutputRight, 1);
    assert(std::fabs(pairOutputLeft[0] - 0.45f) < 0.0001f);
    assert(std::fabs(pairOutputRight[0] + 0.3f) < 0.0001f);

    // Unlinking fans shared gain/mute/solo back to both sides, preserving pan.
    assert(pairController.setAudioInputPairLinked(false));
    assert(std::fabs(pairController.desiredAudioInputPairChannel(0).gain - 0.6f) < 0.00001f);
    assert(std::fabs(pairController.desiredAudioInputPairChannel(1).gain - 0.6f) < 0.00001f);
    assert(pairController.desiredAudioInputPairChannel(0).solo);
    assert(pairController.desiredAudioInputPairChannel(1).solo);
    assert(pairController.setAudioInputPairChannelGain(0, 50));
    assert(pairController.setAudioInputPairChannelPan(0, -100));
    assert(pairController.setAudioInputPairChannelGain(1, 25));
    assert(pairController.setAudioInputPairChannelPan(1, 100));
    assert(pairController.setAudioInputPairChannelMute(1, false));
    assert(pairController.setAudioInputPairChannelSolo(1, false));
    assert(pairController.setAudioInputPairChannelSolo(0, true));
    pairBridge.processWithAudioInputPair(
        noTrackInputs, pairInputLeft, pairInputRight,
        pairOutputLeft, pairOutputRight, 1);
    assert(std::fabs(pairOutputLeft[0] - 0.5f) < 0.0001f);
    assert(std::fabs(pairOutputRight[0]) < 0.0001f);

    // A pair solo suppresses an ordinary non-soloed track.
    const float soloTrackSample[1] = {0.25f};
    const float* mixedInputs[mpcmixer::AudioEngine::kTrackCount]{};
    mixedInputs[0] = soloTrackSample;
    pairBridge.processWithAudioInputPair(
        mixedInputs, pairInputLeft, pairInputRight,
        pairOutputLeft, pairOutputRight, 1);
    assert(std::fabs(pairOutputLeft[0] - 0.5f) < 0.0001f);
    assert(std::fabs(pairOutputRight[0]) < 0.0001f);

    // Track and pair solos sum; a track solo suppresses a non-soloed pair.
    assert(pairController.setTrackSolo(0, true));
    pairBridge.processWithAudioInputPair(
        mixedInputs, pairInputLeft, pairInputRight,
        pairOutputLeft, pairOutputRight, 1);
    assert(std::fabs(pairOutputLeft[0] - (0.5f + 0.25f * std::sqrt(0.5f))) < 0.0001f);
    assert(std::fabs(pairOutputRight[0] - 0.25f * std::sqrt(0.5f)) < 0.0001f);

    assert(pairController.setAudioInputPairChannelSolo(0, false));
    pairBridge.processWithAudioInputPair(
        mixedInputs, pairInputLeft, pairInputRight,
        pairOutputLeft, pairOutputRight, 1);
    assert(std::fabs(pairOutputLeft[0] - 0.25f * std::sqrt(0.5f)) < 0.0001f);
    assert(std::fabs(pairOutputRight[0] - 0.25f * std::sqrt(0.5f)) < 0.0001f);

    assert(pairController.setTrackSolo(0, false));
    pairBridge.processWithAudioInputPair(
        noTrackInputs, pairInputLeft, pairInputRight,
        pairOutputLeft, pairOutputRight, 1);
    assert(std::fabs(pairOutputLeft[0] - 0.5f) < 0.0001f);
    assert(std::fabs(pairOutputRight[0] + 0.125f) < 0.0001f);

    // Track edits are isolated: changing one strip must not overwrite another.
    assert(controller.setTrackFader(4, 55));
    assert(controller.setTrackPan(4, 25));
    assert(std::fabs(controller.desiredTrack(3).gain - 1.27f) < 0.00001f);
    assert(std::fabs(controller.desiredTrack(4).gain - 0.55f) < 0.00001f);
    assert(std::fabs(controller.desiredTrack(4).pan - 0.25f) < 0.00001f);

    // Invalid EQ preparation must not change the producer-side cached bands.
    const auto beforeInvalidEq = controller.desiredTrackEq(4);
    assert(!controller.setTrackEq(4, 0.0f, bands));
    assert(!controller.setTrackEq(4, 48000.0f,
        std::array<mpcmixer::SixBandEq::BandParameters,
                   mpcmixer::SixBandEq::kBandCount>{}));
    assert(std::fabs(controller.desiredTrackEq(4)[1].gainDb -
                     beforeInvalidEq[1].gainDb) < 0.00001f);

    // Invalid Aux return values are rejected without changing the desired copy.
    mpcmixer::AudioEngine::AuxReturn invalidAux;
    invalidAux.gain = -1.0f;
    assert(!controller.setAuxReturn(2, invalidAux));
    assert(std::fabs(controller.desiredAuxReturn(2).gain - 1.0f) < 0.00001f);

    // A full queue rejects track and EQ edits without advancing desired state.
    mpcmixer::AudioEngineCommandBridge saturatedBridge;
    mpcmixer::MixerController saturatedController(saturatedBridge);
    for (std::size_t i = 0; i < mpcmixer::AudioEngineCommandBridge::kQueueCapacity; ++i)
        assert(saturatedController.setMasterMute((i & 1u) != 0));
    const float oldTrackGain = saturatedController.desiredTrack(7).gain;
    const auto oldEq = saturatedController.desiredTrackEq(7);
    assert(!saturatedController.setTrackFader(7, 80));
    auto replacementBands = oldEq;
    replacementBands[0].gainDb = 6.0f;
    assert(!saturatedController.setTrackEq(7, 48000.0f, replacementBands));
    assert(std::fabs(saturatedController.desiredTrack(7).gain - oldTrackGain) <
           0.00001f);
    assert(std::fabs(saturatedController.desiredTrackEq(7)[0].gainDb -
                     oldEq[0].gainDb) < 0.00001f);

    // Linking two previously independent Audio In strips seeds one shared
    // EQ, sample rate and Aux-send state from the left strip atomically.
    mpcmixer::AudioEngineCommandBridge inputPairBridge;
    mpcmixer::MixerController inputPairController(inputPairBridge);
    auto inputLeftEq = mpcmixer::SixBandEq::defaultBandParameters();
    auto inputRightEq = mpcmixer::SixBandEq::defaultBandParameters();
    inputLeftEq[0].gainDb = 3.0f;
    inputRightEq[0].gainDb = -3.0f;
    assert(inputPairController.setAudioInputPairEq(0, 48000.0f, inputLeftEq));
    assert(inputPairController.setAudioInputPairEq(1, 44100.0f, inputRightEq));
    assert(inputPairController.setAudioInputPairAuxSend(0, 1, 0.2f));
    assert(inputPairController.setAudioInputPairAuxSend(1, 1, 0.7f));
    assert(inputPairController.setAudioInputPairLinked(true));
    const auto linkedInputState = inputPairController.desiredStateSnapshot();
    assert(std::fabs(linkedInputState.audioInputEq[0][0].gainDb - 3.0f) < 0.0001f);
    assert(std::fabs(linkedInputState.audioInputEq[1][0].gainDb - 3.0f) < 0.0001f);
    assert(std::fabs(linkedInputState.audioInputEqSampleRates[0] - 48000.0f) < 0.0001f);
    assert(std::fabs(linkedInputState.audioInputEqSampleRates[1] - 48000.0f) < 0.0001f);
    assert(std::fabs(linkedInputState.audioInputStereo.auxSend[1] - 0.2f) < 0.0001f);
    assert(std::fabs(linkedInputState.audioInputChannels[1].auxSend[1] - 0.2f) < 0.0001f);
    const float inputPairLeft[] = {0.5f};
    const float inputPairRight[] = {0.5f};
    float inputPairOutLeft[] = {0.0f};
    float inputPairOutRight[] = {0.0f};
    const float* noInputPairTracks[mpcmixer::AudioEngine::kTrackCount]{};
    inputPairBridge.processWithAudioInputPair(
        noInputPairTracks, inputPairLeft, inputPairRight,
        inputPairOutLeft, inputPairOutRight, 1);
    assert(inputPairBridge.pendingCountApprox() == 0);
    assert(std::isfinite(inputPairOutLeft[0]) &&
           std::isfinite(inputPairOutRight[0]));
    return 0;
}
