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

    std::cout << "Audio engine tests passed\n";
    return 0;
}
