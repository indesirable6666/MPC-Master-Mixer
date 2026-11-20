#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace mpcmixer {

// Six-band parametric EQ stage. Coefficients are prepared by configure()
// outside the audio callback; process() performs no allocation or trig work.
// Each band is an RBJ peaking biquad with independently configurable frequency, Q and gain.
class SixBandEq {
public:
    static constexpr std::size_t kBandCount = 6;
    static constexpr float kMinGainDb = -12.0f;
    static constexpr float kMaxGainDb = 12.0f;
    static constexpr float kMinFrequencyHz = 20.0f;
    static constexpr float kMinQ = 0.1f;
    static constexpr float kMaxQ = 10.0f;

    struct BandParameters {
        float frequencyHz;
        float q;
        float gainDb;
    };

    static std::array<BandParameters, kBandCount> defaultBandParameters() {
        return {{{60.0f, 1.0f, 0.0f}, {180.0f, 1.0f, 0.0f},
                 {500.0f, 1.0f, 0.0f}, {1500.0f, 1.0f, 0.0f},
                 {5000.0f, 1.0f, 0.0f}, {12000.0f, 1.0f, 0.0f}}};
    }

    static float frequencyHz(std::size_t band) {
        static constexpr float frequencies[kBandCount] = {
            60.0f, 180.0f, 500.0f, 1500.0f, 5000.0f, 12000.0f
        };
        return frequencies[band < kBandCount ? band : 0];
    }

    struct Coefficients {
        float b0 = 1.0f;
        float b1 = 0.0f;
        float b2 = 0.0f;
        float a1 = 0.0f;
        float a2 = 0.0f;
    };

    SixBandEq() = default;

    bool configure(float sampleRate,
                   const std::array<BandParameters, kBandCount>& bands) {
        if (!std::isfinite(sampleRate) || sampleRate < 8000.0f ||
            sampleRate > 192000.0f) {
            return false;
        }
        for (const BandParameters& band : bands) {
            if (!std::isfinite(band.frequencyHz) ||
                !std::isfinite(band.q) || !std::isfinite(band.gainDb) ||
                band.frequencyHz < kMinFrequencyHz ||
                band.frequencyHz >= sampleRate * 0.5f ||
                band.q < kMinQ || band.q > kMaxQ ||
                band.gainDb < kMinGainDb || band.gainDb > kMaxGainDb) {
                return false;
            }
        }

        std::array<Coefficients, kBandCount> next{};
        for (std::size_t band = 0; band < kBandCount; ++band) {
            const float amplitude = std::pow(10.0f, bands[band].gainDb / 40.0f);
            const float omega = 2.0f * kPi * bands[band].frequencyHz / sampleRate;
            const float cosine = std::cos(omega);
            const float alpha = std::sin(omega) / (2.0f * bands[band].q);
            const float a0 = 1.0f + alpha / amplitude;
            next[band].b0 = (1.0f + alpha * amplitude) / a0;
            next[band].b1 = (-2.0f * cosine) / a0;
            next[band].b2 = (1.0f - alpha * amplitude) / a0;
            next[band].a1 = (-2.0f * cosine) / a0;
            next[band].a2 = (1.0f - alpha / amplitude) / a0;
        }
        coefficients_ = next;
        bands_ = bands;
        sampleRate_ = sampleRate;
        configured_ = true;
        reset();
        return true;
    }

    // Convenience overload for the six gain controls using the documented
    // default centre frequencies and Q=1.0.
    bool configure(float sampleRate,
                   const std::array<float, kBandCount>& gainsDb) {
        auto bands = defaultBandParameters();
        for (std::size_t band = 0; band < kBandCount; ++band)
            bands[band].gainDb = gainsDb[band];
        return configure(sampleRate, bands);
    }

    const BandParameters& bandParameters(std::size_t band) const {
        return bands_[band < kBandCount ? band : 0];
    }

    bool configured() const { return configured_; }
    float sampleRate() const { return sampleRate_; }
    const Coefficients& coefficients(std::size_t band) const {
        return coefficients_[band < kBandCount ? band : 0];
    }

    void reset() {
        for (auto& channel : state_) {
            for (auto& band : channel) band = State{};
        }
    }

    float processSample(float input, std::size_t channel = 0) {
        if (!std::isfinite(input)) input = 0.0f;
        if (!configured_) return input;
        const std::size_t side = channel < 2 ? channel : 0;
        float sample = input;
        for (std::size_t band = 0; band < kBandCount; ++band) {
            const Coefficients& c = coefficients_[band];
            State& z = state_[side][band];
            const float output = c.b0 * sample + z.z1;
            z.z1 = c.b1 * sample - c.a1 * output + z.z2;
            z.z2 = c.b2 * sample - c.a2 * output;
            sample = std::isfinite(output) ? output : 0.0f;
        }
        return sample;
    }

    // Interleaved-independent planar processing: each channel keeps its own
    // filter history. Null channel buffers are safely ignored.
    void process(float* left, float* right, std::size_t frameCount) {
        if (!configured_) return;
        for (std::size_t frame = 0; frame < frameCount; ++frame) {
            if (left != nullptr) left[frame] = processSample(left[frame], 0);
            if (right != nullptr) right[frame] = processSample(right[frame], 1);
        }
    }

private:
    struct State {
        float z1 = 0.0f;
        float z2 = 0.0f;
    };

    static constexpr float kPi = 3.14159265358979323846f;
    std::array<Coefficients, kBandCount> coefficients_{};
    std::array<BandParameters, kBandCount> bands_ = defaultBandParameters();
    std::array<std::array<State, kBandCount>, 2> state_{};
    float sampleRate_ = 0.0f;
    bool configured_ = false;
};

} // namespace mpcmixer
