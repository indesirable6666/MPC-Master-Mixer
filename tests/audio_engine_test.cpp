#include "audio_engine.hpp"
#include "audio_backend.hpp"
#include "six_band_eq.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>
#include <array>

using mpcmixer::AudioEngine;

namespace {
bool near(float actual, float expected, float tolerance = 0.0001f) {
    return std::fabs(actual - expected) <= tolerance;
}
}

int main() {
    AudioEngine engine;
    float track0[] = {1.0f, 0.5f, -0.5f, 0.0f};
    float track1[] = {0.0f, 0.5f, 0.5f, 1.0f};
    const float* inputs[AudioEngine::kTrackCount] = {};
    inputs[0] = track0;
    inputs[1] = track1;
    float left[4] = {};
    float right[4] = {};

    // Two centred tracks are summed equally into a stereo bus.
    engine.process(inputs, left, right, 4);
    assert(near(left[0], std::sqrt(0.5f)));
    assert(near(right[0], std::sqrt(0.5f)));
    assert(near(left[1], std::sqrt(0.5f)));
    assert(near(right[2], 0.0f));

    // Hard-left and hard-right constant-power panning.
    AudioEngine::Track state;
    state.pan = -1.0f;
    assert(engine.setTrack(0, state));
    state.pan = 1.0f;
    assert(engine.setTrack(1, state));
    engine.process(inputs, left, right, 4);
    assert(near(left[0], 1.0f));
    assert(near(right[0], 0.0f));
    assert(near(left[3], 0.0f));
    assert(near(right[3], 1.0f));

    // The complete 127-step fader range includes +2.1 dB above unity.
    state = AudioEngine::Track{};
    state.gain = AudioEngine::kMaxFaderGain;
    assert(engine.setTrack(0, state));
    assert(near(engine.track(0).gain, 1.27f));

    // Gain and mute.
    state = AudioEngine::Track{};
    state.gain = 0.25f;
    assert(engine.setTrack(0, state));
    state = AudioEngine::Track{};
    state.mute = true;
    assert(engine.setTrack(1, state));
    engine.process(inputs, left, right, 4);
    assert(near(left[0], 0.25f * std::sqrt(0.5f)));
    assert(near(right[0], 0.25f * std::sqrt(0.5f)));

    // Solo isolates the soloed track; mute still takes precedence.
    state = AudioEngine::Track{};
    state.solo = true;
    assert(engine.setTrack(1, state));
    state = AudioEngine::Track{};
    assert(engine.setTrack(0, state));
    engine.process(inputs, left, right, 4);
    assert(near(left[0], 0.0f));
    assert(near(left[1], 0.5f * std::sqrt(0.5f)));

    // Master gain and mute.
    assert(engine.setMasterGain(0.5f));
    engine.process(inputs, left, right, 4);
    assert(near(left[1], 0.25f * std::sqrt(0.5f)));
    engine.setMasterMute(true);
    engine.process(inputs, left, right, 4);
    for (int i = 0; i < 4; ++i) {
        assert(near(left[i], 0.0f));
        assert(near(right[i], 0.0f));
    }

    // Invalid state is rejected without changing the previous valid state.
    assert(!engine.setMasterGain(std::numeric_limits<float>::quiet_NaN()));
    assert(!engine.setMasterGain(1.28f));
    assert(engine.setMasterGain(AudioEngine::kMaxFaderGain));
    assert(!engine.setTrack(16, AudioEngine::Track{}));
    state = AudioEngine::Track{};
    state.pan = 2.0f;
    assert(!engine.setTrack(0, state));
    state = AudioEngine::Track{};
    state.gain = std::numeric_limits<float>::infinity();
    assert(!engine.setTrack(0, state));

    // Non-finite samples are silenced; missing inputs are silence.
    engine.setMasterMute(false);
    assert(engine.setMasterGain(1.0f));
    state = AudioEngine::Track{};
    assert(engine.setTrack(1, state));
    float bad[] = {std::numeric_limits<float>::quiet_NaN()};
    const float* badInputs[AudioEngine::kTrackCount] = {};
    badInputs[0] = bad;
    engine.process(badInputs, left, right, 1);
    assert(near(left[0], 0.0f));
    assert(near(right[0], 0.0f));

    // The channel mixer runs each track's configured EQ before its fader,
    // pan and Aux sends (the default unconfigured EQ is transparent).
    AudioEngine eqEngine;
    AudioEngine::Track eqTrack;
    eqTrack.auxSend[0] = 1.0f;
    assert(eqEngine.setTrack(0, eqTrack));
    auto engineEqBands = mpcmixer::SixBandEq::defaultBandParameters();
    engineEqBands[2].gainDb = 6.0f;
    assert(eqEngine.setTrackEq(0, 48000.0f, engineEqBands));
    float eqImpulse[] = {1.0f, 0.0f, 0.0f, 0.0f};
    const float* eqInputs[AudioEngine::kTrackCount] = {};
    eqInputs[0] = eqImpulse;
    float eqMainL[4] = {}, eqMainR[4] = {};
    float eqAuxL[4] = {}, eqAuxR[4] = {};
    float* eqAuxLeft[AudioEngine::kAuxBusCount] = {eqAuxL, nullptr, nullptr, nullptr};
    float* eqAuxRight[AudioEngine::kAuxBusCount] = {eqAuxR, nullptr, nullptr, nullptr};
    eqEngine.processWithAux(eqInputs, eqMainL, eqMainR,
                            eqAuxLeft, eqAuxRight, 4);
    assert(eqMainL[0] > std::sqrt(0.5f));
    assert(near(eqMainL[0], eqAuxL[0]));
    assert(near(eqMainR[0], eqAuxR[0]));
    assert(!eqEngine.setTrackEq(16, 48000.0f, engineEqBands));

    // A per-track insert runs after EQ and before fader/pan and Aux sends.
    AudioEngine insertEngine;
    mpcmixer::SoftClipInsert softClip;
    assert(softClip.setDrive(2.0f));
    softClip.setEnabled(true);
    assert(insertEngine.setTrackInsert(0, 0, &softClip));
    assert(insertEngine.setTrackInsert(0, 1, nullptr));
    assert(!insertEngine.setTrackInsert(16, 0, &softClip));
    assert(!insertEngine.setTrackInsert(0, 2, &softClip));
    float insertInput[] = {0.75f, -0.75f};
    const float* insertInputs[AudioEngine::kTrackCount] = {};
    insertInputs[0] = insertInput;
    float insertLeft[2] = {}, insertRight[2] = {};
    insertEngine.process(insertInputs, insertLeft, insertRight, 2);
    assert(near(insertLeft[0], std::sqrt(0.5f)));
    assert(near(insertRight[0], std::sqrt(0.5f)));
    assert(near(insertLeft[1], -std::sqrt(0.5f)));
    assert(near(insertRight[1], -std::sqrt(0.5f)));
    assert(!softClip.setDrive(9.0f));
    assert(near(softClip.drive(), 2.0f));
    softClip.setEnabled(false);
    insertEngine.process(insertInputs, insertLeft, insertRight, 1);
    assert(near(insertLeft[0], 0.75f * std::sqrt(0.5f)));
    assert(near(softClip.processSample(std::numeric_limits<float>::quiet_NaN()), 0.0f));

    // The default capture backend is explicit silence and never touches a device.
    mpcmixer::SilentAudioCaptureBackend silentBackend;
    mpcmixer::AudioFormat captureFormat;
    assert(silentBackend.capture(nullptr, 2, 4) ==
           mpcmixer::CaptureStatus::NotPrepared);
    assert(silentBackend.prepare(captureFormat));
    float captureLeft[] = {1.0f, -1.0f, 0.5f, 0.25f};
    float captureRight[] = {-0.5f, 0.5f, 1.0f, -1.0f};
    float* captureBuffers[] = {captureLeft, captureRight};
    assert(silentBackend.capture(captureBuffers, 2, 4) ==
           mpcmixer::CaptureStatus::Silence);
    for (int frame = 0; frame < 4; ++frame) {
        assert(near(captureLeft[frame], 0.0f));
        assert(near(captureRight[frame], 0.0f));
    }
    assert(silentBackend.capture(captureBuffers, 1, 4) ==
           mpcmixer::CaptureStatus::InvalidBuffer);
    assert(silentBackend.capture(captureBuffers, 2, 513) ==
           mpcmixer::CaptureStatus::InvalidBuffer);
    float* invalidCaptureBuffers[] = {captureLeft, nullptr};
    assert(silentBackend.capture(invalidCaptureBuffers, 2, 4) ==
           mpcmixer::CaptureStatus::InvalidBuffer);
    mpcmixer::AudioFormat invalidFormat;
    invalidFormat.sampleRate = 1000;
    assert(!silentBackend.prepare(invalidFormat));
    assert(silentBackend.capture(captureBuffers, 2, 4) ==
           mpcmixer::CaptureStatus::Silence);
    silentBackend.shutdown();
    assert(silentBackend.capture(captureBuffers, 2, 4) ==
           mpcmixer::CaptureStatus::NotPrepared);

    // Four independent stereo Aux sends are post-fader/post-pan and pre-master.
    AudioEngine auxEngine;
    AudioEngine::Track auxTrack;
    auxTrack.gain = 0.5f;
    auxTrack.pan = -1.0f;
    auxTrack.auxSend[0] = 0.5f;
    auxTrack.auxSend[2] = 1.0f;
    assert(auxEngine.setTrack(0, auxTrack));
    float auxInput[] = {1.0f, -0.5f};
    const float* auxInputs[AudioEngine::kTrackCount] = {};
    auxInputs[0] = auxInput;
    float mainL[2] = {}, mainR[2] = {};
    float auxL0[2] = {}, auxR0[2] = {};
    float auxL1[2] = {}, auxR1[2] = {};
    float auxL2[2] = {}, auxR2[2] = {};
    float auxL3[2] = {}, auxR3[2] = {};
    float* auxLeft[AudioEngine::kAuxBusCount] = {
        auxL0, auxL1, auxL2, auxL3
    };
    float* auxRight[AudioEngine::kAuxBusCount] = {
        auxR0, auxR1, auxR2, auxR3
    };
    auxEngine.setMasterGain(0.25f);
    auxEngine.processWithAux(auxInputs, mainL, mainR, auxLeft, auxRight, 2);
    assert(near(mainL[0], 0.125f));
    assert(near(mainR[0], 0.0f));
    assert(near(auxL0[0], 0.25f));
    assert(near(auxR0[0], 0.0f));
    assert(near(auxL1[0], 0.0f));
    assert(near(auxL2[0], 0.5f));
    assert(near(auxL3[0], 0.0f));
    assert(near(auxL0[1], -0.125f));

    // Master mute/gain affect the main bus, not independent Aux sends.
    auxEngine.setMasterMute(true);
    auxEngine.processWithAux(auxInputs, mainL, mainR, auxLeft, auxRight, 1);
    assert(near(mainL[0], 0.0f) && near(mainR[0], 0.0f));
    assert(near(auxL0[0], 0.25f));
    auxTrack.mute = true;
    assert(auxEngine.setTrack(0, auxTrack));
    auxEngine.processWithAux(auxInputs, mainL, mainR, auxLeft, auxRight, 1);
    assert(near(auxL0[0], 0.0f) && near(auxL2[0], 0.0f));

    // Invalid Aux levels are rejected without replacing the previous state.
    auxTrack.mute = false;
    auxTrack.auxSend[1] = std::numeric_limits<float>::quiet_NaN();
    assert(!auxEngine.setTrack(0, auxTrack));
    assert(near(auxEngine.track(0).auxSend[0], 0.5f));
    auxTrack.auxSend[1] = 1.01f;
    assert(!auxEngine.setTrack(0, auxTrack));

    // Six-band parametric EQ: unity response, independent channel state,
    // bounded configuration, reset, and safe handling of non-finite samples.
    mpcmixer::SixBandEq eq;
    std::array<float, mpcmixer::SixBandEq::kBandCount> eqGains{};
    assert(eq.configure(48000.0f, eqGains));
    assert(eq.configured());
    assert(near(eq.processSample(0.75f, 0), 0.75f));
    assert(near(eq.processSample(-0.25f, 1), -0.25f));

    eqGains[2] = 6.0f;
    assert(eq.configure(48000.0f, eqGains));
    float eqLeft[] = {1.0f, 0.0f, 0.0f, 0.0f};
    float eqRight[] = {0.0f, 0.0f, 0.0f, 0.0f};
    eq.process(eqLeft, eqRight, 4);
    assert(std::fabs(eqLeft[0] - 1.0f) > 0.0001f);
    for (float sample : eqRight) assert(near(sample, 0.0f));

    auto parametricBands = mpcmixer::SixBandEq::defaultBandParameters();
    parametricBands[3].frequencyHz = 2300.0f;
    parametricBands[3].q = 2.2f;
    parametricBands[3].gainDb = -4.5f;
    assert(eq.configure(48000.0f, parametricBands));
    assert(near(eq.bandParameters(3).frequencyHz, 2300.0f));
    assert(near(eq.bandParameters(3).q, 2.2f));
    assert(near(eq.bandParameters(3).gainDb, -4.5f));
    parametricBands[3].q = 0.05f;
    assert(!eq.configure(48000.0f, parametricBands));
    parametricBands[3].q = 1.0f;
    parametricBands[3].frequencyHz = 24000.0f;
    assert(!eq.configure(48000.0f, parametricBands));
    const auto validEqCoefficients = eq.coefficients(2);
    eqGains[0] = 13.0f;
    assert(!eq.configure(48000.0f, eqGains));
    assert(near(eq.coefficients(2).b0, validEqCoefficients.b0));
    assert(!eq.configure(7000.0f, std::array<float, 6>{}));
    eqGains.fill(0.0f);
    assert(!eq.configure(24000.0f, eqGains)); // 12 kHz is Nyquist.
    assert(eq.configure(48000.0f, std::array<float, 6>{}));
    assert(near(eq.processSample(std::numeric_limits<float>::infinity()), 0.0f));
    eq.reset();
    assert(near(eq.processSample(0.5f), 0.5f));

    // Verify the parametric band's actual steady-state magnitude at its
    // centre frequency, not merely that an impulse response changes.
    auto responseBands = mpcmixer::SixBandEq::defaultBandParameters();
    responseBands[3].frequencyHz = 2300.0f;
    responseBands[3].q = 2.2f;
    responseBands[3].gainDb = -4.5f;
    mpcmixer::SixBandEq responseEq;
    assert(responseEq.configure(48000.0f, responseBands));
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    constexpr int kResponseFrames = 48000;
    constexpr int kWarmupFrames = 4000;
    for (int frame = 0; frame < kResponseFrames + kWarmupFrames; ++frame) {
        const float sample = std::sin(2.0f * 3.14159265358979323846f *
                                      2300.0f * frame / 48000.0f);
        const float processed = responseEq.processSample(sample);
        if (frame >= kWarmupFrames) {
            inputEnergy += static_cast<double>(sample) * sample;
            outputEnergy += static_cast<double>(processed) * processed;
        }
    }
    const float measuredRatio = static_cast<float>(
        std::sqrt(outputEnergy / inputEnergy));
    const float expectedRatio = std::pow(10.0f, -4.5f / 20.0f);
    assert(near(measuredRatio, expectedRatio, 0.002f));

    // Stereo Aux returns are mixed into the master bus with their own
    // gain, balance and mute, and are not fed back into Aux sends.
    AudioEngine returnEngine;
    AudioEngine::AuxReturn returnState;
    returnState.gain = 0.5f;
    returnState.balance = 1.0f;
    assert(returnEngine.setAuxReturn(0, returnState));
    assert(!returnEngine.setAuxReturn(4, returnState));
    returnState.gain = 1.28f;
    assert(!returnEngine.setAuxReturn(0, returnState));
    returnState.gain = 0.5f;
    assert(returnEngine.setAuxReturn(0, returnState));
    float returnL[] = {1.0f, 0.5f};
    float returnR[] = {0.5f, -1.0f};
    const float* returnInputsL[AudioEngine::kAuxBusCount] = {
        returnL, nullptr, nullptr, nullptr
    };
    const float* returnInputsR[AudioEngine::kAuxBusCount] = {
        returnR, nullptr, nullptr, nullptr
    };
    float returnMainL[2] = {}, returnMainR[2] = {};
    float unusedAuxL[2] = {}, unusedAuxR[2] = {};
    float* returnSendL[AudioEngine::kAuxBusCount] = {
        unusedAuxL, nullptr, nullptr, nullptr
    };
    float* returnSendR[AudioEngine::kAuxBusCount] = {
        unusedAuxR, nullptr, nullptr, nullptr
    };
    returnEngine.setMasterGain(0.5f);
    returnEngine.processWithAuxAndReturns(
        nullptr, returnMainL, returnMainR, returnSendL, returnSendR,
        returnInputsL, returnInputsR, 2);
    assert(near(returnMainL[0], 0.0f));
    assert(near(returnMainR[0], 0.125f));
    assert(near(returnMainR[1], -0.25f));
    assert(near(unusedAuxL[0], 0.0f));
    assert(near(unusedAuxR[0], 0.0f));
    returnState.mute = true;
    assert(returnEngine.setAuxReturn(0, returnState));
    returnEngine.processWithAuxAndReturns(
        nullptr, returnMainL, returnMainR, returnSendL, returnSendR,
        returnInputsL, returnInputsR, 1);
    assert(near(returnMainL[0], 0.0f) && near(returnMainR[0], 0.0f));

    // Two Audio In mono channels can be linked without collapsing stereo.
    mpcmixer::AudioInputPair audioIn;
    float inputL[] = {1.0f, 0.5f};
    float inputR[] = {-0.25f, 0.75f};
    float pairL[2] = {};
    float pairR[2] = {};
    audioIn.setLinked(true);
    audioIn.process(inputL, inputR, pairL, pairR, 2);
    assert(near(pairL[0], 1.0f));
    assert(near(pairR[0], -0.25f));
    assert(near(pairL[1], 0.5f));
    assert(near(pairR[1], 0.75f));

    mpcmixer::AudioInputPair::StereoControls stereo;
    stereo.gain = 0.5f;
    stereo.balance = 1.0f;
    assert(audioIn.setStereoControls(stereo));
    audioIn.process(inputL, inputR, pairL, pairR, 2);
    assert(near(pairL[0], 0.0f));
    assert(near(pairR[0], -0.125f));
    assert(near(pairL[1], 0.0f));
    assert(near(pairR[1], 0.375f));

    stereo.balance = 0.0f;
    stereo.mute = true;
    assert(audioIn.setStereoControls(stereo));
    audioIn.process(inputL, inputR, pairL, pairR, 2);
    assert(near(pairL[0], 0.0f) && near(pairR[0], 0.0f));

    // Unlinked mode restores two independently pannable mono inputs.
    audioIn.setLinked(false);
    mpcmixer::AudioInputPair::Channel leftChannel;
    leftChannel.pan = -1.0f;
    leftChannel.gain = 0.5f;
    mpcmixer::AudioInputPair::Channel rightChannel;
    rightChannel.pan = 1.0f;
    rightChannel.gain = 0.25f;
    assert(audioIn.setChannel(0, leftChannel));
    assert(audioIn.setChannel(1, rightChannel));
    audioIn.process(inputL, inputR, pairL, pairR, 2);
    assert(near(pairL[0], 0.5f));
    assert(near(pairR[0], -0.0625f));
    leftChannel.solo = true;
    assert(audioIn.setChannel(0, leftChannel));
    audioIn.process(inputL, inputR, pairL, pairR, 2);
    assert(near(pairR[0], 0.0f));

    // Invalid stereo controls and channel values are rejected.
    stereo.gain = 1.28f;
    assert(!audioIn.setStereoControls(stereo));
    leftChannel.pan = std::numeric_limits<float>::quiet_NaN();
    assert(!audioIn.setChannel(0, leftChannel));
    // Rejected controls leave the previous valid state untouched.
    assert(near(audioIn.stereoControls().gain, 0.5f));
    assert(audioIn.stereoControls().mute);
    assert(near(audioIn.channel(0).pan, -1.0f));

    // Linked processing treats missing and non-finite samples as silence.
    stereo = mpcmixer::AudioInputPair::StereoControls{};
    assert(audioIn.setStereoControls(stereo));
    audioIn.setLinked(true);
    float invalidPair[] = {
        std::numeric_limits<float>::quiet_NaN(),
        std::numeric_limits<float>::infinity()
    };
    pairL[0] = pairL[1] = 9.0f;
    pairR[0] = pairR[1] = 9.0f;
    audioIn.process(invalidPair, nullptr, pairL, pairR, 2);
    assert(near(pairL[0], 0.0f) && near(pairL[1], 0.0f));
    assert(near(pairR[0], 0.0f) && near(pairR[1], 0.0f));

    // A zero-frame block is a safe no-op.
    pairL[0] = 7.0f;
    pairR[0] = 8.0f;
    audioIn.process(inputL, inputR, pairL, pairR, 0);
    assert(near(pairL[0], 7.0f) && near(pairR[0], 8.0f));

    std::cout << "Audio engine tests passed\n";
    return 0;
}
