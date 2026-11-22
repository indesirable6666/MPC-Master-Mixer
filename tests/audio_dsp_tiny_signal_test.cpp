#include "../src/audio/audio_engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>

int main() {
    using namespace mpcmixer;

    constexpr float sampleRate = 48000.0f;
    const float tiny = std::numeric_limits<float>::denorm_min();

    // Exercise all six EQ sections with values at and near the subnormal
    // range. This is a numerical-safety test, not a timing benchmark.
    SixBandEq eq;
    auto bands = SixBandEq::defaultBandParameters();
    for (std::size_t i = 0; i < bands.size(); ++i)
        bands[i].gainDb = (i % 2 == 0) ? 12.0f : -12.0f;
    assert(eq.configure(sampleRate, bands));

    for (int i = 0; i < 20000; ++i) {
        const float input = (i % 3 == 0) ? tiny :
            ((i % 3 == 1) ? 1.0e-30f : -1.0e-25f);
        const float output = eq.processSample(input, static_cast<std::size_t>(i % 2));
        assert(std::isfinite(output));
    }

    // A non-finite EQ input is replaced with zero. Reset first so the
    // expected output is exactly zero rather than a legitimate filter tail.
    eq.reset();
    assert(eq.processSample(std::numeric_limits<float>::quiet_NaN()) == 0.0f);
    assert(eq.processSample(std::numeric_limits<float>::infinity()) == 0.0f);
    assert(std::isfinite(eq.processSample(0.25f)));

    OnePoleLowPassInsert lowPass;
    assert(lowPass.configure(sampleRate, 1000.0f));
    lowPass.setEnabled(true);
    OnePoleHighPassInsert highPass;
    assert(highPass.configure(sampleRate, 1000.0f));
    highPass.setEnabled(true);

    for (int i = 0; i < 20000; ++i) {
        const float input = (i % 2 == 0) ? tiny : -1.0e-30f;
        assert(std::isfinite(lowPass.processSample(input)));
        assert(std::isfinite(highPass.processSample(input)));
    }

    // Stateful filters can legitimately emit a finite decay tail after a
    // non-finite sample has been sanitized to zero; assert safety, not silence.
    assert(std::isfinite(lowPass.processSample(std::numeric_limits<float>::quiet_NaN())));
    assert(std::isfinite(highPass.processSample(std::numeric_limits<float>::infinity())));
    assert(std::isfinite(lowPass.processSample(0.5f)));
    assert(std::isfinite(highPass.processSample(0.5f)));

    return 0;
}
