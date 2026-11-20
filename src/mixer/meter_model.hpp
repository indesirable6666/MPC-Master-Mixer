#pragma once

#include <algorithm>
#include <cmath>

namespace mpcmixer {

// UI-side meter state only. Values are normalized linear amplitudes [0,1].
// This class does not capture audio or read MPC/ALSA signal data.
class MeterModel {
public:
    static constexpr float kClipThreshold = 0.999f;

    void update(float peak, float rms) {
        if (!std::isfinite(peak) || !std::isfinite(rms) ||
            peak < 0.0f || rms < 0.0f) {
            return;
        }
        peak_ = std::max(peak_, std::min(peak, 1.0f));
        rms_ = std::min(rms, 1.0f);
        if (peak_ >= kClipThreshold) {
            clipped_ = true;
        }
    }

    void decayPeak(float amount) {
        if (!std::isfinite(amount) || amount < 0.0f) {
            return;
        }
        peak_ = std::max(0.0f, peak_ - amount);
    }

    void resetClip() { clipped_ = false; }
    void reset() {
        peak_ = 0.0f;
        rms_ = 0.0f;
        clipped_ = false;
    }

    float peak() const { return peak_; }
    float rms() const { return rms_; }
    bool clipped() const { return clipped_; }

private:
    float peak_ = 0.0f;
    float rms_ = 0.0f;
    bool clipped_ = false;
};

} // namespace mpcmixer
