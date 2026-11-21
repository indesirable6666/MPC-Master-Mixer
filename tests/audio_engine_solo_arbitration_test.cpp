#include "../src/audio/audio_engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 1.0e-5f; }
}

int main() {
    using Engine = mpcmixer::AudioEngine;
    Engine engine;

    std::array<std::array<float, 1>, Engine::kTrackCount> samples{};
    std::array<const float*, Engine::kTrackCount> inputs{};
    float left[1]{};
    float right[1]{};
    float expectedAll = 0.0f;

    for (std::size_t i = 0; i < Engine::kTrackCount; ++i) {
        samples[i][0] = 0.1f * static_cast<float>(i + 1);
        inputs[i] = samples[i].data();
        Engine::Track state;
        state.gain = 1.0f;
        state.pan = 0.0f;
        state.solo = (i == 2 || i == 8);
        state.mute = (i == 4 || i == 8); // Mute takes precedence over Solo.
        assert(engine.setTrack(i, state));
        if (i != 4 && i != 8) expectedAll += samples[i][0];
    }

    // With any Solo active, only soloed and non-muted strips contribute.
    // Track 8 is soloed but muted; all other strips are excluded.
    engine.process(inputs.data(), left, right, 1);
    const float centerPan = std::sqrt(0.5f);
    assert(near(left[0], samples[2][0] * centerPan));
    assert(near(right[0], samples[2][0] * centerPan));

    // Clearing the last Solo restores every non-muted strip.
    Engine::Track soloOff;
    soloOff.gain = 1.0f;
    soloOff.pan = 0.0f;
    soloOff.mute = true; // preserve track 4's mute
    assert(engine.setTrack(4, soloOff));
    Engine::Track otherSoloOff;
    otherSoloOff.gain = 1.0f;
    otherSoloOff.pan = 0.0f;
    otherSoloOff.mute = true; // preserve track 8's mute
    assert(engine.setTrack(8, otherSoloOff));

    engine.process(inputs.data(), left, right, 1);
    assert(near(left[0], expectedAll * centerPan));
    assert(near(right[0], expectedAll * centerPan));

    // A soloed strip remains audible alongside another soloed strip;
    // removing one solo must not accidentally clear the other.
    Engine::Track soloA;
    soloA.gain = 1.0f;
    soloA.pan = 0.0f;
    soloA.solo = true;
    assert(engine.setTrack(1, soloA));
    Engine::Track soloB;
    soloB.gain = 1.0f;
    soloB.pan = 0.0f;
    soloB.solo = true;
    assert(engine.setTrack(10, soloB));
    engine.process(inputs.data(), left, right, 1);
    assert(near(left[0], (samples[1][0] + samples[10][0]) * centerPan));

    soloA.solo = false;
    assert(engine.setTrack(1, soloA));
    engine.process(inputs.data(), left, right, 1);
    assert(near(left[0], samples[10][0] * centerPan));

    return 0;
}
