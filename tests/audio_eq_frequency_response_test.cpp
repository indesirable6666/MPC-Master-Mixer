#include "../src/audio/audio_engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSampleRate = 48000.0f;
constexpr std::size_t kWarmupFrames = 24000;
constexpr std::size_t kMeasureFrames = 48000;

template <typename Processor>
float measureRatio(Processor processor, float frequencyHz) {
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    for (std::size_t frame = 0;
         frame < kWarmupFrames + kMeasureFrames; ++frame) {
        const float input = std::sin(
            2.0f * kPi * frequencyHz * static_cast<float>(frame) /
            kSampleRate);
        const float output = processor(input);
        if (frame >= kWarmupFrames) {
            inputEnergy += static_cast<double>(input) * input;
            outputEnergy += static_cast<double>(output) * output;
        }
    }
    assert(inputEnergy > 0.0);
    return static_cast<float>(std::sqrt(outputEnergy / inputEnergy));
}

bool near(float actual, float expected, float tolerance) {
    return std::fabs(actual - expected) <= tolerance;
}
} // namespace

int main() {
    const auto defaults = mpcmixer::SixBandEq::defaultBandParameters();

    // Exercise each parametric band independently at its own centre
    // frequency. A peaking EQ's centre gain is approximately its requested
    // dB value; allow a small numerical tolerance for float processing.
    for (std::size_t band = 0; band < mpcmixer::SixBandEq::kBandCount; ++band) {
        auto boosted = defaults;
        boosted[band].gainDb = 6.0f;
        mpcmixer::SixBandEq boostEq;
        assert(boostEq.configure(kSampleRate, boosted));
        const float boostRatio = measureRatio(
            [&boostEq](float sample) { return boostEq.processSample(sample); },
            defaults[band].frequencyHz);
        assert(near(boostRatio, std::pow(10.0f, 6.0f / 20.0f), 0.025f));

        auto cut = defaults;
        cut[band].gainDb = -6.0f;
        mpcmixer::SixBandEq cutEq;
        assert(cutEq.configure(kSampleRate, cut));
        const float cutRatio = measureRatio(
            [&cutEq](float sample) { return cutEq.processSample(sample); },
            defaults[band].frequencyHz);
        assert(near(cutRatio, std::pow(10.0f, -6.0f / 20.0f), 0.015f));
    }

    // Verify the actual AudioEngine signal path: EQ runs before the insert,
    // then the track fader and centre equal-power pan feed the stereo output.
    mpcmixer::AudioEngine engine;
    auto engineBands = defaults;
    engineBands[2].gainDb = 6.0f;
    assert(engine.setTrackEq(0, kSampleRate, engineBands));

    mpcmixer::GainInsert trim;
    assert(trim.setGain(0.5f));
    trim.setEnabled(true);
    assert(engine.setTrackInsert(0, 0, &trim));

    mpcmixer::AudioEngine::Track track;
    track.gain = 0.8f;
    track.pan = 0.0f;
    assert(engine.setTrack(0, track));

    std::array<float, kWarmupFrames + kMeasureFrames> input{};
    std::array<float, kWarmupFrames + kMeasureFrames> left{};
    std::array<float, kWarmupFrames + kMeasureFrames> right{};
    for (std::size_t frame = 0; frame < input.size(); ++frame) {
        input[frame] = std::sin(
            2.0f * kPi * defaults[2].frequencyHz *
            static_cast<float>(frame) / kSampleRate);
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
    const float actualLeftRatio =
        static_cast<float>(std::sqrt(outputEnergy / inputEnergy));
    const float expectedLeftRatio =
        std::pow(10.0f, 6.0f / 20.0f) * 0.5f * 0.8f *
        std::sqrt(0.5f);
    assert(near(actualLeftRatio, expectedLeftRatio, 0.02f));

    return 0;
}
