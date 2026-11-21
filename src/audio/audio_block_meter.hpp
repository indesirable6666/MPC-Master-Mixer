#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>

namespace mpcmixer {

// Result for one contiguous mono audio block. Amplitudes are normalized to
// [0,1]; values above full scale are clamped for meter presentation.
struct AudioBlockMeterReading {
    float peak = 0.0f;
    float rms = 0.0f;
};

// Stateless, allocation-free measurement helper. It does not publish across
// threads or access an audio device. Call it on a valid block, then transfer
// the result to the UI through an explicitly synchronized control path.
class AudioBlockMeter {
public:
    static bool measure(const float* samples, std::size_t frameCount,
                        AudioBlockMeterReading& result) {
        if (samples == nullptr || frameCount == 0) {
            return false;
        }

        double sumSquares = 0.0;
        double peak = 0.0;
        for (std::size_t i = 0; i < frameCount; ++i) {
            const double sample = static_cast<double>(samples[i]);
            if (!std::isfinite(sample)) {
                return false;
            }
            const double magnitude = std::fabs(sample);
            peak = std::max(peak, magnitude);
            sumSquares += sample * sample;
        }

        const double meanSquare =
            sumSquares / static_cast<double>(frameCount);
        AudioBlockMeterReading measured;
        measured.peak = static_cast<float>(std::min(peak, 1.0));
        measured.rms = static_cast<float>(
            std::min(std::sqrt(meanSquare), 1.0));
        result = measured;
        return true;
    }
};

} // namespace mpcmixer
