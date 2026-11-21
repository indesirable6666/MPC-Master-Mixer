#include "../src/audio/audio_engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSampleRate = 48000.0f;
constexpr float kToneHz = 500.0f;
constexpr float kLowPassHz = 3000.0f;
constexpr std::size_t kWarmupFrames = 24000;
constexpr std::size_t kMeasureFrames = 48000;

float measureCombinedPath(bool lowPassEnabled) {
    mpcmixer::AudioEngine engine;
    auto bands = mpcmixer::SixBandEq::defaultBandParameters();
    bands[2].gainDb = 6.0f;
    assert(engine.setTrackEq(0, kSampleRate, bands));

    mpcmixer::OnePoleLowPassInsert lowPass;
    assert(lowPass.configure(kSampleRate, kLowPassHz));
    lowPass.setEnabled(lowPassEnabled);

    mpcmixer::GainInsert trim;
    assert(trim.setGain(0.5f));
    trim.setEnabled(true);

    assert(engine.setTrackInsert(0, 0, &lowPass));
    assert(engine.setTrackInsert(0, 1, &trim));

    mpcmixer::AudioEngine::Track track;
    track.gain = 0.8f;
    track.pan = 0.0f;
    assert(engine.setTrack(0, track));

    std::array<float, kWarmupFrames + kMeasureFrames> input{};
    std::array<float, kWarmupFrames + kMeasureFrames> left{};
    std::array<float, kWarmupFrames + kMeasureFrames> right{};
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        input[frame] = std::sin(
            2.0f * kPi * kToneHz * static_cast<float>(frame) / kSampleRate);
    }

    const float* inputs[mpcmixer::AudioEngine::kTrackCount] = {};
    inputs[0] = input.data();
    engine.process(inputs, left.data(), right.data(), input.size());

    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    for (std::size_t frame = kWarmupFrames; frame < input.size(); ++frame) {
        inputEnergy += static_cast<double>(input[frame]) * input[frame];
        outputEnergy += static_cast<double>(left[frame]) * left[frame];
    }
    assert(inputEnergy > 0.0);
    return static_cast<float>(std::sqrt(outputEnergy / inputEnergy));
}

bool near(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}
} // namespace

int main() {
    // One-pole low-pass magnitude at the test tone:
    // H(z) = alpha / (1 - (1-alpha) z^-1).
    const float alpha = static_cast<float>(
        1.0 - std::exp(-2.0 * static_cast<double>(kPi) * kLowPassHz /
                       static_cast<double>(kSampleRate)));
    const float omega = 2.0f * kPi * kToneHz / kSampleRate;
    const float denominatorReal = 1.0f - (1.0f - alpha) * std::cos(omega);
    const float denominatorImag = (1.0f - alpha) * std::sin(omega);
    const float lowPassMagnitude = alpha /
        std::sqrt(denominatorReal * denominatorReal +
                  denominatorImag * denominatorImag);

    // Verify the combined real track path: +6 dB EQ, low-pass insert,
    // 0.5x gain insert, 0.8x fader and centre equal-power pan.
    const float fixedStages = std::pow(10.0f, 6.0f / 20.0f) *
        0.5f * 0.8f * std::sqrt(0.5f);
    const float enabledRatio = measureCombinedPath(true);
    assert(near(enabledRatio, fixedStages * lowPassMagnitude, 0.015f));

    // Bypassing only the low-pass must remove its frequency-dependent loss,
    // while EQ and the second insert remain active.
    const float bypassedRatio = measureCombinedPath(false);
    assert(near(bypassedRatio, fixedStages, 0.015f));
    assert(bypassedRatio > enabledRatio);

    return 0;
}
