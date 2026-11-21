#include "audio_engine.hpp"
#include "audio_duplex_backend.hpp"
#include "audio_duplex_engine_host.hpp"
#include "audio_control_bridge.hpp"
#include "../src/mixer/mixer_controller.hpp"
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

    // UI/control commands are queued by the producer and applied only at
    // the next audio block boundary, avoiding setter/process data races.
    mpcmixer::AudioEngineCommandBridge bridge;
    mpcmixer::AudioEngine::Track bridgeTrack;
    bridgeTrack.gain = 0.5f;
    assert(bridge.enqueueTrack(0, bridgeTrack));
    assert(bridge.enqueueMasterGain(0.5f));
    assert(bridge.pendingCountApprox() == 2);
    float bridgeInput[] = {1.0f};
    const float* bridgeInputs[AudioEngine::kTrackCount] = {};
    bridgeInputs[0] = bridgeInput;
    float bridgeLeft[1] = {}, bridgeRight[1] = {};
    bridge.process(bridgeInputs, bridgeLeft, bridgeRight, 1);
    assert(bridge.pendingCountApprox() == 0);
    assert(near(bridgeLeft[0], 0.25f * std::sqrt(0.5f)));
    assert(near(bridgeRight[0], 0.25f * std::sqrt(0.5f)));
    assert(bridge.enqueueMasterMute(true));
    bridge.process(bridgeInputs, bridgeLeft, bridgeRight, 1);
    assert(near(bridgeLeft[0], 0.0f) && near(bridgeRight[0], 0.0f));
    assert(!bridge.enqueueMasterGain(std::numeric_limits<float>::quiet_NaN()));
    bridgeTrack.pan = 2.0f;
    assert(!bridge.enqueueTrack(0, bridgeTrack));
    // EQ coefficients are prepared on the producer thread, queued by value,
    // and copied/reset at the next audio block without coefficient trig work.
    mpcmixer::AudioEngineCommandBridge eqBridge;
    auto queuedEqBands = mpcmixer::SixBandEq::defaultBandParameters();
    queuedEqBands[2].gainDb = 6.0f;
    assert(eqBridge.enqueueTrackEq(0, 48000.0f, queuedEqBands));
    float queuedEqImpulse[] = {1.0f};
    const float* queuedEqInputs[AudioEngine::kTrackCount] = {};
    queuedEqInputs[0] = queuedEqImpulse;
    float queuedEqLeft[1] = {}, queuedEqRight[1] = {};
    eqBridge.process(queuedEqInputs, queuedEqLeft, queuedEqRight, 1);
    assert(queuedEqLeft[0] > std::sqrt(0.5f));
    assert(near(queuedEqLeft[0], queuedEqRight[0]));
    assert(!eqBridge.enqueueTrackEq(0, 48000.0f, std::array<mpcmixer::SixBandEq::BandParameters, 6>{}));

    // Insert replacement is acknowledged at a block boundary. The old effect
    // remains alive until the producer observes the ticket, then may be retired
    // off the audio thread.
    mpcmixer::AudioEngineCommandBridge insertBridge;
    mpcmixer::SoftClipInsert oldInsert;
    mpcmixer::SoftClipInsert newInsert;
    assert(oldInsert.setDrive(1.5f));
    assert(newInsert.setDrive(2.0f));
    oldInsert.setEnabled(true);
    newInsert.setEnabled(true);
    std::uint32_t firstInsertTicket = 0;
    std::uint32_t replacementInsertTicket = 0;
    assert(insertBridge.enqueueTrackInsert(0, 0, &oldInsert, firstInsertTicket));
    assert(!insertBridge.hasApplied(firstInsertTicket));
    assert(insertBridge.enqueueTrackInsert(0, 0, &newInsert, replacementInsertTicket));
    assert(!insertBridge.hasApplied(replacementInsertTicket));
    float insertBridgeInput[] = {0.5f};
    const float* insertBridgeInputs[AudioEngine::kTrackCount] = {};
    insertBridgeInputs[0] = insertBridgeInput;
    float insertBridgeLeft[1] = {}, insertBridgeRight[1] = {};
    insertBridge.process(insertBridgeInputs, insertBridgeLeft, insertBridgeRight, 1);
    assert(insertBridge.hasApplied(firstInsertTicket));
    assert(insertBridge.hasApplied(replacementInsertTicket));
    assert(near(insertBridgeLeft[0], std::sqrt(0.5f)));
    assert(near(insertBridgeRight[0], std::sqrt(0.5f)));
    assert(insertBridge.engineOnAudioThread().track(0).gain == 1.0f);
    assert(!insertBridge.enqueueTrackInsert(16, 0, nullptr, replacementInsertTicket));
    assert(!insertBridge.enqueueTrackInsert(0, 2, nullptr, replacementInsertTicket));

    mpcmixer::AudioEngineCommandBridge returnBridge;
    mpcmixer::AudioEngine::AuxReturn bridgeReturn;
    bridgeReturn.gain = 0.5f;
    bridgeReturn.balance = 1.0f;
    assert(returnBridge.enqueueAuxReturn(0, bridgeReturn));
    float bridgeReturnL[] = {1.0f};
    float bridgeReturnR[] = {1.0f};
    const float* bridgeReturnsL[AudioEngine::kAuxBusCount] = {
        bridgeReturnL, nullptr, nullptr, nullptr
    };
    const float* bridgeReturnsR[AudioEngine::kAuxBusCount] = {
        bridgeReturnR, nullptr, nullptr, nullptr
    };
    returnBridge.processWithAuxAndReturns(
        nullptr, bridgeLeft, bridgeRight, nullptr, nullptr,
        bridgeReturnsL, bridgeReturnsR, 1);
    assert(near(bridgeLeft[0], 0.0f));
    assert(near(bridgeRight[0], 0.5f));

    mpcmixer::AudioEngineCommandBridge fullBridge;
    for (std::size_t i = 0; i < mpcmixer::AudioEngineCommandBridge::kQueueCapacity; ++i)
        assert(fullBridge.enqueueMasterMute((i % 2) != 0));
    assert(!fullBridge.enqueueMasterMute(true));
    assert(fullBridge.pendingCountApprox() ==
           mpcmixer::AudioEngineCommandBridge::kQueueCapacity);
    fullBridge.process(nullptr, bridgeLeft, bridgeRight, 0);
    assert(fullBridge.pendingCountApprox() == 0);

    // Native UI controller maps integer mixer controls to validated DSP state.
    mpcmixer::AudioEngineCommandBridge controllerBridge;
    mpcmixer::MixerController controller(controllerBridge);
    assert(controller.setTrackFader(2, 127));
    assert(controller.setTrackPan(2, -50));
    assert(controller.setTrackMute(2, true));
    assert(controller.setTrackSolo(2, true));
    assert(controller.setTrackAuxSend(2, 3, 75));
    assert(!controller.setTrackFader(2, 128));
    assert(!controller.setTrackFader(2, -1));
    assert(!controller.setTrackPan(2, 101));
    assert(!controller.setTrackPan(2, -101));
    assert(!controller.setTrackAuxSend(2, 4, 101));
    assert(!controller.setTrackAuxSend(2, 0, -1));
    assert(!controller.setTrackAuxSend(16, 0, 50));
    assert(controller.setMasterFader(100));
    assert(controller.setMasterMute(true));
    float controllerInput[] = {1.0f};
    const float* controllerInputs[AudioEngine::kTrackCount] = {};
    controllerInputs[2] = controllerInput;
    float controllerLeft[1] = {}, controllerRight[1] = {};
    controllerBridge.process(controllerInputs, controllerLeft, controllerRight, 1);
    const auto& controlled = controllerBridge.engineOnAudioThread().track(2);
    assert(near(controlled.gain, 1.27f));
    assert(near(controlled.pan, -0.5f));
    assert(controlled.mute && controlled.solo);
    assert(near(controlled.auxSend[3], 0.75f));
    assert(near(controllerBridge.engineOnAudioThread().masterGain(), 1.0f));
    assert(controllerBridge.engineOnAudioThread().masterMute());
    // A full queue does not advance the controller's cached desired state.
    mpcmixer::AudioEngineCommandBridge controllerFullBridge;
    mpcmixer::MixerController fullController(controllerFullBridge);
    for (std::size_t i = 0; i < mpcmixer::AudioEngineCommandBridge::kQueueCapacity; ++i)
        assert(controllerFullBridge.enqueueMasterMute((i % 2) != 0));
    assert(!fullController.setTrackFader(0, 80));
    assert(near(fullController.desiredTrack(0).gain, 1.0f));

    // The duplex host maps physical inputs only through explicit routes.
    mpcmixer::AudioEngineCommandBridge hostedBridge;
    mpcmixer::AudioEngine::Track hostedTrack;
    hostedTrack.gain = 0.5f;
    assert(hostedBridge.enqueueTrack(3, hostedTrack));
    hostedTrack.gain = 0.25f;
    assert(hostedBridge.enqueueTrack(4, hostedTrack));
    mpcmixer::AudioDuplexEngineHost host(hostedBridge);
    assert(host.inputRoute(0) == mpcmixer::AudioDuplexEngineHost::kUnmappedTrack);
    assert(host.setInputRoute(0, 3));
    assert(host.setInputRoute(1, 4));
    assert(!host.setInputRoute(2, 1));
    assert(host.setInputRoute(0, mpcmixer::AudioDuplexEngineHost::kUnmappedTrack));
    assert(host.setInputRoute(0, 3));
    assert(!host.setInputRoute(0, 17));
    mpcmixer::OfflineAudioDuplexBackend hostedBackend;
    mpcmixer::AudioDuplexFormat hostedFormat;
    assert(host.prepare(hostedBackend, hostedFormat));
    assert(!host.prepare(hostedBackend, hostedFormat));
    assert(host.start());
    float hostedInputL[] = {1.0f, 0.5f};
    float hostedInputR[] = {-0.5f, 0.25f};
    const float* hostedInputs[] = {hostedInputL, hostedInputR};
    float hostedOutputL[2] = {}, hostedOutputR[2] = {};
    float* hostedOutputs[] = {hostedOutputL, hostedOutputR};
    mpcmixer::AudioDuplexBuffer hostedBlock{hostedInputs, hostedOutputs, 2, 2, 2};
    assert(hostedBackend.processBlock(hostedBlock) ==
           mpcmixer::AudioDuplexStatus::Processed);
    assert(near(hostedOutputL[0], 0.375f * std::sqrt(0.5f)));
    assert(near(hostedOutputR[0], 0.375f * std::sqrt(0.5f)));
    assert(host.setInputRoute(1, 3));
    hostedOutputL[0] = hostedOutputR[0] = 7.0f;
    host.processBlock(hostedBlock);
    assert(near(hostedOutputL[0], 0.0f) && near(hostedOutputR[0], 0.0f));
    // A routed channel with a missing input buffer must silence both outputs.
    assert(host.setInputRoute(1, 4));
    const float* malformedInputs[] = {hostedInputL, nullptr};
    hostedOutputL[0] = hostedOutputR[0] = 9.0f;
    mpcmixer::AudioDuplexBuffer missingInputBlock{
        malformedInputs, hostedOutputs, 2, 2, 2};
    host.processBlock(missingInputBlock);
    assert(near(hostedOutputL[0], 0.0f) && near(hostedOutputR[0], 0.0f));

    // If one output plane is missing, any available plane is still silenced.
    float* malformedOutputs[] = {hostedOutputL, nullptr};
    hostedOutputL[0] = 9.0f;
    mpcmixer::AudioDuplexBuffer missingOutputBlock{
        hostedInputs, malformedOutputs, 2, 2, 2};
    host.processBlock(missingOutputBlock);
    assert(near(hostedOutputL[0], 0.0f));

    // A malformed output-channel count clears every available supported
    // output plane without indexing beyond the supplied pointer array.
    float* monoOutput[] = {hostedOutputL};
    hostedOutputL[0] = 5.0f;
    mpcmixer::AudioDuplexBuffer monoOutputBlock{
        hostedInputs, monoOutput, 2, 1, 2};
    host.processBlock(monoOutputBlock);
    assert(near(hostedOutputL[0], 0.0f));

    float hostedExtraOutput[2] = {4.0f, 4.0f};
    float* tripleOutputs[] = {hostedOutputL, hostedOutputR, hostedExtraOutput};
    hostedOutputL[0] = hostedOutputR[0] = 3.0f;
    mpcmixer::AudioDuplexBuffer tripleOutputBlock{
        hostedInputs, tripleOutputs, 2, 3, 2};
    host.processBlock(tripleOutputBlock);
    assert(near(hostedOutputL[0], 0.0f) && near(hostedOutputR[0], 0.0f));
    assert(near(hostedExtraOutput[0], 4.0f));

    // A callback with a channel count different from the prepared format is
    // rejected safely and clears valid output planes.
    const float* shortInputs[] = {hostedInputL};
    hostedOutputL[0] = hostedOutputR[0] = 8.0f;
    mpcmixer::AudioDuplexBuffer wrongInputCount{
        shortInputs, hostedOutputs, 1, 2, 2};
    host.processBlock(wrongInputCount);
    assert(near(hostedOutputL[0], 0.0f) && near(hostedOutputR[0], 0.0f));

    // Oversized blocks are ignored: the host cannot assume the backing buffers
    // are as large as an invalid frameCount claims.
    hostedOutputL[0] = hostedOutputR[0] = 6.0f;
    mpcmixer::AudioDuplexBuffer oversizedHostedBlock{
        hostedInputs, hostedOutputs, 2, 2, hostedFormat.maxFramesPerBlock + 1};
    host.processBlock(oversizedHostedBlock);
    assert(near(hostedOutputL[0], 6.0f) && near(hostedOutputR[0], 6.0f));

    host.stop();
    host.shutdown();

    // Full-duplex callback contract: caller-owned planar buffers are passed
    // through the offline driver without copying or allocating.
    struct DuplexProbe { std::size_t calls = 0; float firstInput = 0.0f; };
    DuplexProbe duplexProbe;
    const auto duplexCallback = [](void* context,
                                   const mpcmixer::AudioDuplexBuffer& block) noexcept {
        DuplexProbe* probe = static_cast<DuplexProbe*>(context);
        ++probe->calls;
        probe->firstInput = block.inputChannels[0][0];
        for (std::size_t i = 0; i < block.frameCount; ++i) {
            block.outputChannels[0][i] = block.inputChannels[0][i];
            block.outputChannels[1][i] = block.inputChannels[1][i];
        }
    };
    mpcmixer::OfflineAudioDuplexBackend duplex;
    mpcmixer::AudioDuplexFormat duplexFormat;
    assert(duplex.prepare(duplexFormat, duplexCallback, &duplexProbe));
    assert(!duplex.prepare(duplexFormat, duplexCallback, &duplexProbe));
    assert(duplex.start());
    float duplexInL[] = {0.25f, -0.5f}, duplexInR[] = {-0.75f, 1.0f};
    float duplexOutL[2] = {}, duplexOutR[2] = {};
    const float* duplexInputs[] = {duplexInL, duplexInR};
    float* duplexOutputs[] = {duplexOutL, duplexOutR};
    mpcmixer::AudioDuplexBuffer duplexBlock{duplexInputs, duplexOutputs, 2, 2, 2};
    assert(duplex.processBlock(duplexBlock) == mpcmixer::AudioDuplexStatus::Processed);
    assert(duplexProbe.calls == 1 && near(duplexProbe.firstInput, 0.25f));
    assert(near(duplexOutL[1], -0.5f) && near(duplexOutR[1], 1.0f));
    duplexBlock.frameCount = duplexFormat.maxFramesPerBlock + 1;
    assert(duplex.processBlock(duplexBlock) == mpcmixer::AudioDuplexStatus::InvalidBuffer);
    duplex.stop();
    assert(duplex.processBlock(duplexBlock) == mpcmixer::AudioDuplexStatus::NotPrepared);
    duplex.shutdown();
    assert(!duplex.prepare(duplexFormat, nullptr, &duplexProbe));

    std::cout << "Audio engine tests passed\n";
    return 0;
}
