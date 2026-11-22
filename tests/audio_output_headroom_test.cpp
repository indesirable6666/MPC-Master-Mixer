#include "../src/audio/audio_engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

int main() {
    using namespace mpcmixer;

    AudioEngine engine;
    std::array<std::array<float, 4>, AudioEngine::kTrackCount> storage{};
    const float* inputs[AudioEngine::kTrackCount] = {};
    for (std::size_t track = 0; track < AudioEngine::kTrackCount; ++track) {
        storage[track].fill(1.0f);
        inputs[track] = storage[track].data();

        AudioEngine::Track state;
        state.gain = AudioEngine::kMaxFaderGain;
        assert(engine.setTrack(track, state));
    }
    assert(engine.setMasterGain(AudioEngine::kMaxFaderGain));

    float left[4] = {};
    float right[4] = {};
    engine.process(inputs, left, right, 4);

    // The DSP bus is intentionally floating point and unclipped. Sixteen
    // full-scale centred tracks at maximum strip/master gain must exceed 1.0.
    const float expected = 16.0f * AudioEngine::kMaxFaderGain *
        AudioEngine::kMaxFaderGain * std::sqrt(0.5f);
    assert(expected > 1.0f);
    for (std::size_t frame = 0; frame < 4; ++frame) {
        assert(std::isfinite(left[frame]));
        assert(std::isfinite(right[frame]));
        assert(std::fabs(left[frame] - expected) < 0.0001f);
        assert(std::fabs(right[frame] - expected) < 0.0001f);
    }

    // Master mute remains a hard zero even when the pre-mute mix exceeds unity.
    engine.setMasterMute(true);
    engine.process(inputs, left, right, 4);
    for (std::size_t frame = 0; frame < 4; ++frame) {
        assert(left[frame] == 0.0f);
        assert(right[frame] == 0.0f);
    }

    return 0;
}
