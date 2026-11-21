#include "../src/audio/audio_engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
constexpr std::size_t kFrames = 64;
constexpr float kInput = 0.5f;

float runTrack(mpcmixer::AudioEngine& engine) {
    std::array<float, kFrames> input{};
    std::array<float, kFrames> left{};
    std::array<float, kFrames> right{};
    input.fill(kInput);
    const float* inputs[mpcmixer::AudioEngine::kTrackCount] = {};
    inputs[0] = input.data();
    engine.process(inputs, left.data(), right.data(), kFrames);
    return left[kFrames - 1] * 1.41421356237f; // Undo centre equal-power pan.
}

bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 0.00001f;
}
} // namespace

int main() {
    mpcmixer::AudioEngine engine;
    mpcmixer::AudioEngine::Track track;
    track.pan = 0.0f;
    assert(engine.setTrack(0, track));

    // Slot order is observable: clip first, then halve the result.
    // Reversing the two effects produces a different nonlinear result.
    mpcmixer::SoftClipInsert clip;
    assert(clip.setDrive(2.0f));
    clip.setEnabled(true);
    mpcmixer::GainInsert trim;
    assert(trim.setGain(0.5f));
    trim.setEnabled(true);
    assert(engine.setTrackInsert(0, 0, &clip));
    assert(engine.setTrackInsert(0, 1, &trim));

    assert(near(runTrack(engine), 0.5f)); // clip(0.5 * 2) * 0.5

    // Bypassing slot 0 leaves slot 1 active.
    clip.setEnabled(false);
    assert(near(runTrack(engine), 0.25f));

    // Re-enable slot 0 and bypass slot 1.
    clip.setEnabled(true);
    trim.setEnabled(false);
    assert(near(runTrack(engine), 1.0f)); // SoftClip(0.5, drive=2)

    // Both bypassed is a transparent unity path.
    clip.setEnabled(false);
    assert(near(runTrack(engine), 0.5f));

    // An empty slot is transparent and invalid slot indices are rejected.
    assert(!engine.setTrackInsert(0, 2, &clip));
    assert(engine.setTrackInsert(0, 1, nullptr));
    clip.setEnabled(true);
    assert(near(runTrack(engine), 1.0f));

    return 0;
}
