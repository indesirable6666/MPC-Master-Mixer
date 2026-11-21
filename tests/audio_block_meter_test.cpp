#include "../src/audio/audio_block_meter.hpp"

#include <cassert>
#include <cmath>
#include <limits>

namespace {
bool near(float actual, float expected, float tolerance = 0.00001f) {
    return std::fabs(actual - expected) <= tolerance;
}
}

int main() {
    mpcmixer::AudioBlockMeterReading reading;
    reading.peak = 0.25f;
    reading.rms = 0.125f;

    assert(!mpcmixer::AudioBlockMeter::measure(nullptr, 4, reading));
    assert(!mpcmixer::AudioBlockMeter::measure(nullptr, 0, reading));
    assert(near(reading.peak, 0.25f));
    assert(near(reading.rms, 0.125f));

    const float silence[] = {0.0f, 0.0f, 0.0f, 0.0f};
    assert(mpcmixer::AudioBlockMeter::measure(silence, 4, reading));
    assert(reading.peak == 0.0f && reading.rms == 0.0f);

    const float mixed[] = {1.0f, -1.0f, 0.0f, 0.0f};
    assert(mpcmixer::AudioBlockMeter::measure(mixed, 4, reading));
    assert(near(reading.peak, 1.0f));
    assert(near(reading.rms, std::sqrt(0.5f)));

    const float constant[] = {0.5f, -0.5f, 0.5f, -0.5f};
    assert(mpcmixer::AudioBlockMeter::measure(constant, 4, reading));
    assert(near(reading.peak, 0.5f));
    assert(near(reading.rms, 0.5f));

    const float overRange[] = {2.0f, -2.0f, 2.0f, -2.0f};
    assert(mpcmixer::AudioBlockMeter::measure(overRange, 4, reading));
    assert(reading.peak == 1.0f && reading.rms == 1.0f);

    const float invalid[] = {0.25f, std::numeric_limits<float>::quiet_NaN()};
    const float invalidInfinity[] = {
        0.25f, std::numeric_limits<float>::infinity()
    };
    const float invalidNegativeInfinity[] = {
        0.25f, -std::numeric_limits<float>::infinity()
    };
    reading.peak = 0.75f;
    reading.rms = 0.5f;
    assert(!mpcmixer::AudioBlockMeter::measure(invalid, 2, reading));
    assert(!mpcmixer::AudioBlockMeter::measure(invalidInfinity, 2, reading));
    assert(!mpcmixer::AudioBlockMeter::measure(
        invalidNegativeInfinity, 2, reading));
    assert(near(reading.peak, 0.75f));
    assert(near(reading.rms, 0.5f));
    return 0;
}
