#include "audio_engine.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>
#include <iostream>
#include <limits>

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
    assert(!engine.setMasterGain(1.1f));
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
    stereo.gain = 2.0f;
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
