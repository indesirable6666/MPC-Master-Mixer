#include "../src/mixer/meter_model.hpp"

#include <cassert>
#include <cmath>
#include <limits>

int main() {
    mpcmixer::MeterModel meter;
    assert(meter.peak() == 0.0f);
    assert(meter.rms() == 0.0f);
    assert(!meter.clipped());

    meter.update(0.4f, 0.2f);
    assert(std::fabs(meter.peak() - 0.4f) < 0.00001f);
    assert(std::fabs(meter.rms() - 0.2f) < 0.00001f);

    // Peak is a hold value; RMS follows the most recent valid update.
    meter.update(0.1f, 0.3f);
    assert(std::fabs(meter.peak() - 0.4f) < 0.00001f);
    assert(std::fabs(meter.rms() - 0.3f) < 0.00001f);

    // Values are clamped to the normalized display range.
    meter.update(2.0f, 1.5f);
    assert(meter.peak() == 1.0f);
    assert(meter.rms() == 1.0f);
    assert(meter.clipped());

    // Invalid input pairs are ignored atomically.
    meter.update(std::numeric_limits<float>::quiet_NaN(), 0.1f);
    assert(meter.peak() == 1.0f);
    assert(meter.rms() == 1.0f);
    meter.update(0.1f, std::numeric_limits<float>::infinity());
    assert(meter.peak() == 1.0f);
    assert(meter.rms() == 1.0f);
    meter.update(-0.1f, 0.1f);
    assert(meter.peak() == 1.0f && meter.rms() == 1.0f);

    meter.decayPeak(0.25f);
    assert(std::fabs(meter.peak() - 0.75f) < 0.00001f);
    meter.decayPeak(-1.0f);
    meter.decayPeak(std::numeric_limits<float>::quiet_NaN());
    assert(std::fabs(meter.peak() - 0.75f) < 0.00001f);
    meter.decayPeak(2.0f);
    assert(meter.peak() == 0.0f);

    meter.update(0.999f, 0.5f);
    assert(meter.clipped());
    meter.resetClip();
    assert(!meter.clipped());
    assert(meter.peak() == 0.999f);

    meter.reset();
    assert(meter.peak() == 0.0f);
    assert(meter.rms() == 0.0f);
    assert(!meter.clipped());
    return 0;
}
