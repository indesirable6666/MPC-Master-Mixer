#include "../src/audio/audio_duplex_engine_host.hpp"

#include <cassert>
#include <cmath>
#include <limits>
#include <memory>
#include <vector>
#include "../src/mixer/mixer_controller.hpp"
#include "../src/mixer/mixer_insert_effect_owner.hpp"

namespace {
bool near(float actual, float expected, float tolerance = 0.0001f) {
    return std::fabs(actual - expected) <= tolerance;
}
}

int main() {
    mpcmixer::AudioEngineCommandBridge commands;
    mpcmixer::AudioMeterSnapshotBridge meters;
    mpcmixer::AudioDuplexEngineHost host(commands, &meters);
    mpcmixer::OfflineAudioDuplexBackend backend;

    assert(host.setStereoInputPairEnabled(true));
    assert(host.stereoInputPairEnabled());
    assert(!host.setInputRoute(0, 3));
    assert(!host.setInputRoute(1, 4));

    mpcmixer::AudioDuplexFormat invalidFormat;
    invalidFormat.inputChannels = 1;
    assert(!host.prepare(backend, invalidFormat));
    assert(!host.start());

    mpcmixer::AudioDuplexFormat format;
    format.sampleRate = 48000;
    format.inputChannels = 2;
    format.outputChannels = 2;
    format.maxFramesPerBlock = 8;
    assert(host.prepare(backend, format));
    assert(!host.setStereoInputPairEnabled(false));
    assert(host.start());

    const float inputLeft[] = {1.0f, 0.5f, -0.5f};
    const float inputRight[] = {-0.25f, 0.75f, 0.5f};
    const float* inputs[] = {inputLeft, inputRight};
    float outputLeft[3]{};
    float outputRight[3]{};
    float* outputs[] = {outputLeft, outputRight};
    const mpcmixer::AudioDuplexBuffer block{inputs, outputs, 2, 2, 3};
    mpcmixer::AudioMeterSnapshot snapshot;

    mpcmixer::AudioInputPair::StereoControls stereo;
    stereo.gain = 0.5f;
    assert(commands.enqueueAudioInputPairLinked(true));
    assert(commands.enqueueAudioInputPairStereoControls(stereo));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.5f));
    assert(near(outputRight[0], -0.125f));
    assert(near(outputLeft[1], 0.25f));
    assert(near(outputRight[1], 0.375f));
    assert(meters.consume(snapshot));
    assert(snapshot.blockSequence == 1);
    assert(near(snapshot.channels[mpcmixer::AudioDuplexEngineHost::kAudioInputLeftTrackIndex].peak, 1.0f));
    assert(near(snapshot.channels[mpcmixer::AudioDuplexEngineHost::kAudioInputRightTrackIndex].peak, 0.75f));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak, 0.5f));

    // Linked balance attenuates only the opposite side and never collapses L/R.
    stereo.balance = 1.0f;
    assert(commands.enqueueAudioInputPairStereoControls(stereo));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.0f));
    assert(near(outputRight[0], -0.125f));
    assert(meters.consume(snapshot));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak, 0.375f));

    // Negative balance attenuates the right input; the midpoint is linear.
    stereo.balance = -1.0f;
    assert(commands.enqueueAudioInputPairStereoControls(stereo));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.5f));
    assert(near(outputRight[0], 0.0f));
    assert(meters.consume(snapshot));

    stereo.balance = -0.5f;
    assert(commands.enqueueAudioInputPairStereoControls(stereo));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.5f));
    assert(near(outputRight[0], -0.0625f));
    assert(meters.consume(snapshot));

    stereo.balance = 0.5f;
    assert(commands.enqueueAudioInputPairStereoControls(stereo));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.25f));
    assert(near(outputRight[0], -0.125f));
    assert(meters.consume(snapshot));

    stereo.mute = true;
    assert(commands.enqueueAudioInputPairStereoControls(stereo));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    for (int i = 0; i < 3; ++i)
        assert(near(outputLeft[i], 0.0f) && near(outputRight[i], 0.0f));
    assert(meters.consume(snapshot));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak, 0.0f));

    // Unlinked mode restores independent mono gain, pan, mute and solo.
    mpcmixer::AudioInputPair::Channel left;
    left.gain = 0.5f;
    left.pan = -1.0f;
    mpcmixer::AudioInputPair::Channel right;
    right.gain = 0.25f;
    right.pan = 1.0f;
    assert(commands.enqueueAudioInputPairLinked(false));
    assert(commands.enqueueAudioInputPairChannel(0, left));
    assert(commands.enqueueAudioInputPairChannel(1, right));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.5f));
    assert(near(outputRight[0], -0.0625f));
    assert(meters.consume(snapshot));

    left.solo = true;
    assert(commands.enqueueAudioInputPairChannel(0, left));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.5f));
    assert(near(outputRight[0], 0.0f));
    assert(meters.consume(snapshot));

    // Master gain and mute apply to the stereo pair as well as normal tracks.
    assert(commands.enqueueMasterGain(0.5f));
    assert(commands.enqueueMasterMute(false));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.25f));
    assert(near(outputRight[0], 0.0f));
    assert(meters.consume(snapshot));

    assert(commands.enqueueMasterMute(true));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    for (int i = 0; i < 3; ++i)
        assert(near(outputLeft[i], 0.0f) && near(outputRight[i], 0.0f));
    assert(meters.consume(snapshot));

    mpcmixer::AudioInputPair::StereoControls invalidStereo;
    invalidStereo.balance = std::numeric_limits<float>::quiet_NaN();
    assert(!commands.enqueueAudioInputPairStereoControls(invalidStereo));
    mpcmixer::AudioInputPair::Channel invalidChannel;
    invalidChannel.gain = 1.28f;
    assert(!commands.enqueueAudioInputPairChannel(0, invalidChannel));
    assert(!commands.enqueueAudioInputPairChannel(2, left));

    host.stop();
    host.shutdown();
    assert(host.setStereoInputPairEnabled(false));
    assert(!host.stereoInputPairEnabled());
    assert(host.setInputRoute(0, 3));
    assert(host.inputRoute(0) == 3);

    // Audio In now shares the track DSP stages: per-side six-band EQ and
    // insert chains run before the linked stereo fader and Aux send taps.
    mpcmixer::AudioEngineCommandBridge pairBridge;
    mpcmixer::MixerController pairController(pairBridge);
    assert(pairController.setAudioInputPairLinked(true));
    assert(pairController.setAudioInputPairAuxSend(0, 0, 0.25f));
    assert(!pairController.setAudioInputPairAuxSend(2, 0, 0.25f));
    assert(!pairController.setAudioInputPairAuxSend(0, 0, 1.01f));

    auto pairBands = mpcmixer::SixBandEq::defaultBandParameters();
    pairBands[3].gainDb = 6.0f; // 1.5 kHz band, at its centre frequency below.
    assert(pairController.setAudioInputPairEq(0, 48000.0f, pairBands));
    mpcmixer::MixerInsertEffectOwner pairInsertOwner(pairController);
    std::unique_ptr<mpcmixer::AudioInsert> rightTrim =
        std::make_unique<mpcmixer::GainInsert>();
    assert(rightTrim->setGain(0.5f));
    rightTrim->setEnabled(true);
    std::uint32_t pairInsertTicket = 0;
    assert(pairInsertOwner.publishAudioInputPair(
        1, 0, rightTrim, pairInsertTicket));

    constexpr std::size_t pairFrames = 48000;
    std::vector<float> pairInputLeft(pairFrames);
    std::vector<float> pairInputRight(pairFrames);
    std::vector<float> pairOutputLeft(pairFrames, 0.0f);
    std::vector<float> pairOutputRight(pairFrames, 0.0f);
    std::vector<float> pairAuxLeft(pairFrames, 0.0f);
    std::vector<float> pairAuxRight(pairFrames, 0.0f);
    constexpr float pairPi = 3.14159265358979323846f;
    for (std::size_t frame = 0; frame < pairFrames; ++frame) {
        const float sample = 0.1f * std::sin(
            2.0f * pairPi * 1500.0f * static_cast<float>(frame) / 48000.0f);
        pairInputLeft[frame] = sample;
        pairInputRight[frame] = sample;
    }
    const float* noTrackInputs[mpcmixer::AudioEngine::kTrackCount]{};
    std::array<float*, mpcmixer::AudioEngine::kAuxBusCount> pairAuxL{};
    std::array<float*, mpcmixer::AudioEngine::kAuxBusCount> pairAuxR{};
    pairAuxL[0] = pairAuxLeft.data();
    pairAuxR[0] = pairAuxRight.data();
    pairBridge.processWithAudioInputPair(
        noTrackInputs, pairInputLeft.data(), pairInputRight.data(),
        pairOutputLeft.data(), pairOutputRight.data(), pairFrames,
        pairAuxL.data(), pairAuxR.data());
    assert(pairController.isAudioInputPairInsertUpdateApplied(pairInsertTicket));
    assert(pairInsertOwner.collectApplied() == 1);

    double leftEnergy = 0.0;
    double rightEnergy = 0.0;
    for (std::size_t frame = pairFrames / 2; frame < pairFrames; ++frame) {
        leftEnergy += static_cast<double>(pairOutputLeft[frame]) *
                      pairOutputLeft[frame];
        rightEnergy += static_cast<double>(pairOutputRight[frame]) *
                       pairOutputRight[frame];
        assert(near(pairAuxLeft[frame], pairOutputLeft[frame] * 0.25f, 0.00001f));
        assert(near(pairAuxRight[frame], pairOutputRight[frame] * 0.25f, 0.00001f));
    }
    const double leftRms = std::sqrt(leftEnergy / (pairFrames / 2));
    const double rightRms = std::sqrt(rightEnergy / (pairFrames / 2));
    // LINK shares EQ coefficients across both stereo channels; the right
    // channel's 0.5x insert is therefore the only 2:1 level difference.
    assert(leftRms > rightRms * 1.85 && leftRms < rightRms * 2.15);
    const auto pairState = pairController.desiredStateSnapshot();
    assert(near(pairState.audioInputEq[0][3].gainDb, 6.0f));
    assert(near(pairState.audioInputEq[1][3].gainDb, 6.0f));
    assert(near(pairState.audioInputEqSampleRates[0], 48000.0f));
    assert(near(pairState.audioInputEqSampleRates[1], 48000.0f));
    assert(near(pairState.audioInputStereo.auxSend[0], 0.25f));

    // Master mute silences the direct pair but, like regular tracks, does not
    // remove its pre-Master Aux sends.
    assert(pairController.setMasterMute(true));
    pairBridge.processWithAudioInputPair(
        noTrackInputs, pairInputLeft.data(), pairInputRight.data(),
        pairOutputLeft.data(), pairOutputRight.data(), pairFrames,
        pairAuxL.data(), pairAuxR.data());
    bool foundPreMasterSend = false;
    for (std::size_t frame = 0; frame < pairFrames; ++frame) {
        assert(near(pairOutputLeft[frame], 0.0f));
        assert(near(pairOutputRight[frame], 0.0f));
        if (std::fabs(pairAuxLeft[frame]) > 0.001f) foundPreMasterSend = true;
    }
    assert(foundPreMasterSend);

    // Unlinked mono sends follow each channel's independent constant-power pan.
    mpcmixer::AudioInputPair unlinkedPair;
    mpcmixer::AudioInputPair::Channel unlinkedLeft;
    unlinkedLeft.gain = 0.5f;
    unlinkedLeft.pan = -1.0f;
    unlinkedLeft.auxSend[0] = 0.5f;
    mpcmixer::AudioInputPair::Channel unlinkedRight;
    unlinkedRight.gain = 0.25f;
    unlinkedRight.pan = 1.0f;
    unlinkedRight.auxSend[0] = 1.0f;
    assert(unlinkedPair.setChannel(0, unlinkedLeft));
    assert(unlinkedPair.setChannel(1, unlinkedRight));
    const float monoLeftInput[] = {1.0f};
    const float monoRightInput[] = {0.5f};
    float monoLeftOutput[] = {0.0f};
    float monoRightOutput[] = {0.0f};
    float monoAuxLeft[] = {0.0f};
    float monoAuxRight[] = {0.0f};
    std::array<float*, mpcmixer::AudioEngine::kAuxBusCount> monoAuxL{};
    std::array<float*, mpcmixer::AudioEngine::kAuxBusCount> monoAuxR{};
    monoAuxL[0] = monoAuxLeft;
    monoAuxR[0] = monoAuxRight;
    unlinkedPair.processAndMix(
        monoLeftInput, monoRightInput, monoLeftOutput, monoRightOutput,
        1, 1.0f, false, false, monoAuxL.data(), monoAuxR.data());
    assert(near(monoLeftOutput[0], 0.5f));
    assert(near(monoRightOutput[0], 0.125f));
    assert(near(monoAuxLeft[0], 0.25f));
    assert(near(monoAuxRight[0], 0.125f));
    return 0;
}
