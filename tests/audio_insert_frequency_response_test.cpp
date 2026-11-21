#include "../src/audio/insert_effects.hpp"

#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
constexpr float kPi = 3.14159265358979323846f;
constexpr float kSampleRate = 48000.0f;
constexpr std::size_t kWarmupFrames = 4000;
constexpr std::size_t kMeasureFrames = 48000;

float measuredRmsRatio(mpcmixer::AudioInsert& effect, float frequencyHz) {
    double inputEnergy = 0.0;
    double outputEnergy = 0.0;
    for (std::size_t frame = 0;
         frame < kWarmupFrames + kMeasureFrames; ++frame) {
        const float input = std::sin(
            2.0f * kPi * frequencyHz * static_cast<float>(frame) /
            kSampleRate);
        const float output = effect.processSample(input);
        if (frame >= kWarmupFrames) {
            inputEnergy += static_cast<double>(input) * input;
            outputEnergy += static_cast<double>(output) * output;
        }
    }
    assert(inputEnergy > 0.0);
    return static_cast<float>(std::sqrt(outputEnergy / inputEnergy));
}

bool between(float value, float minimum, float maximum) {
    return value >= minimum && value <= maximum;
}
} // namespace

int main() {
    // A one-pole low-pass should preserve a low tone and attenuate a tone
    // one decade above its cutoff. This checks steady-state frequency
    // behaviour rather than only the first impulse samples.
    mpcmixer::OnePoleLowPassInsert lowPassLow;
    assert(lowPassLow.configure(kSampleRate, 1000.0f));
    lowPassLow.setEnabled(true);
    const float lowPassPassband = measuredRmsRatio(lowPassLow, 100.0f);
    assert(lowPassPassband > 0.98f);

    mpcmixer::OnePoleLowPassInsert lowPassHigh;
    assert(lowPassHigh.configure(kSampleRate, 1000.0f));
    lowPassHigh.setEnabled(true);
    const float lowPassStopband = measuredRmsRatio(lowPassHigh, 10000.0f);
    assert(between(lowPassStopband, 0.05f, 0.16f));

    // The one-pole high-pass has the complementary broad-band behaviour:
    // it rejects a tone one decade below cutoff and preserves a high tone.
    mpcmixer::OnePoleHighPassInsert highPassLow;
    assert(highPassLow.configure(kSampleRate, 1000.0f));
    highPassLow.setEnabled(true);
    const float highPassStopband = measuredRmsRatio(highPassLow, 100.0f);
    assert(between(highPassStopband, 0.05f, 0.16f));

    mpcmixer::OnePoleHighPassInsert highPassHigh;
    assert(highPassHigh.configure(kSampleRate, 1000.0f));
    highPassHigh.setEnabled(true);
    const float highPassPassband = measuredRmsRatio(highPassHigh, 10000.0f);
    assert(highPassPassband > 0.98f);

    // Soft clipping remains odd-symmetric and bounded for finite overloads.
    mpcmixer::SoftClipInsert clipper;
    assert(clipper.setDrive(2.0f));
    clipper.setEnabled(true);
    const float positive = clipper.processSample(0.3f);
    const float negative = clipper.processSample(-0.3f);
    assert(std::fabs(positive + negative) < 0.000001f);
    assert(std::fabs(positive) < 1.0f);
    assert(clipper.processSample(2.0f) == 1.0f);
    assert(clipper.processSample(-2.0f) == -1.0f);

    return 0;
}
