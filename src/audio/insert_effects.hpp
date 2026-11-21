#pragma once

#include <array>
#include <cmath>
#include <cstddef>

namespace mpcmixer {

// Insert effects are non-owning, allocation-free audio-thread processors.
// Configure and replace them on the control thread, and keep each effect alive
// until the audio callback has stopped using its slot.
class AudioInsert {
public:
    virtual ~AudioInsert() = default;
    virtual float processSample(float input) noexcept = 0;
};

// Simple utility gain insert for trimming or boosting a track before its
// fader. Parameter changes are made off the audio thread; replace the
// configured instance through AudioEngineCommandBridge when it is live.
class GainInsert final : public AudioInsert {
public:
    bool setGain(float gain) noexcept {
        if (!std::isfinite(gain) || gain < 0.0f || gain > 4.0f)
            return false;
        gain_ = gain;
        return true;
    }

    float gain() const noexcept { return gain_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool enabled() const noexcept { return enabled_; }

    float processSample(float input) noexcept override {
        if (!std::isfinite(input)) input = 0.0f;
        if (!enabled_) return input;
        const float output = input * gain_;
        return std::isfinite(output) ? output : 0.0f;
    }

private:
    float gain_ = 1.0f;
    bool enabled_ = false;
};


// First-order low-pass insert. Configure it before publication, then treat the
// instance as immutable while the audio callback can reach it. The single
// state variable is owned by the track's audio callback.
class OnePoleLowPassInsert final : public AudioInsert {
public:
    bool configure(float sampleRate, float cutoffHz) noexcept {
        if (!std::isfinite(sampleRate) || !std::isfinite(cutoffHz) ||
            sampleRate <= 0.0f || cutoffHz <= 0.0f ||
            cutoffHz >= sampleRate * 0.5f)
            return false;
        const double exponent = -6.28318530717958647692 *
            static_cast<double>(cutoffHz) / static_cast<double>(sampleRate);
        const float coefficient = static_cast<float>(1.0 - std::exp(exponent));
        if (!std::isfinite(coefficient) || coefficient <= 0.0f ||
            coefficient > 1.0f)
            return false;
        sampleRate_ = sampleRate;
        cutoffHz_ = cutoffHz;
        coefficient_ = coefficient;
        previousOutput_ = 0.0f;
        configured_ = true;
        return true;
    }

    float sampleRate() const noexcept { return sampleRate_; }
    float cutoffHz() const noexcept { return cutoffHz_; }
    bool configured() const noexcept { return configured_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool enabled() const noexcept { return enabled_; }

    float processSample(float input) noexcept override {
        if (!std::isfinite(input)) input = 0.0f;
        if (!configured_) return input;
        if (!enabled_) {
            // Keep the filter state tracking the bypassed signal so re-enabling
            // it does not resume from a stale sample.
            previousOutput_ = input;
            return input;
        }
        const float output = previousOutput_ +
            coefficient_ * (input - previousOutput_);
        previousOutput_ = std::isfinite(output) ? output : 0.0f;
        return previousOutput_;
    }

private:
    float sampleRate_ = 0.0f;
    float cutoffHz_ = 0.0f;
    float coefficient_ = 0.0f;
    float previousOutput_ = 0.0f;
    bool configured_ = false;
    bool enabled_ = false;
};



// First-order high-pass insert. Configure before publication and keep the
// instance immutable while the audio callback can reach it. State is local
// to the audio callback; bypass tracks the input to avoid a stale edge.
class OnePoleHighPassInsert final : public AudioInsert {
public:
    bool configure(float sampleRate, float cutoffHz) noexcept {
        if (!std::isfinite(sampleRate) || !std::isfinite(cutoffHz) ||
            sampleRate <= 0.0f || cutoffHz <= 0.0f ||
            cutoffHz >= sampleRate * 0.5f)
            return false;
        const double exponent = -6.28318530717958647692 *
            static_cast<double>(cutoffHz) / static_cast<double>(sampleRate);
        const float coefficient = static_cast<float>(std::exp(exponent));
        if (!std::isfinite(coefficient) || coefficient <= 0.0f ||
            coefficient >= 1.0f)
            return false;
        sampleRate_ = sampleRate;
        cutoffHz_ = cutoffHz;
        coefficient_ = coefficient;
        previousInput_ = 0.0f;
        previousOutput_ = 0.0f;
        configured_ = true;
        return true;
    }

    float sampleRate() const noexcept { return sampleRate_; }
    float cutoffHz() const noexcept { return cutoffHz_; }
    bool configured() const noexcept { return configured_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool enabled() const noexcept { return enabled_; }

    float processSample(float input) noexcept override {
        if (!std::isfinite(input)) input = 0.0f;
        if (!configured_) return input;
        if (!enabled_) {
            previousInput_ = input;
            previousOutput_ = 0.0f;
            return input;
        }
        const float output = coefficient_ *
            (previousOutput_ + input - previousInput_);
        previousInput_ = input;
        previousOutput_ = std::isfinite(output) ? output : 0.0f;
        return previousOutput_;
    }

private:
    float sampleRate_ = 0.0f;
    float cutoffHz_ = 0.0f;
    float coefficient_ = 0.0f;
    float previousInput_ = 0.0f;
    float previousOutput_ = 0.0f;
    bool configured_ = false;
    bool enabled_ = false;
};

class SoftClipInsert final : public AudioInsert {
public:
    bool setDrive(float drive) noexcept {
        if (!std::isfinite(drive) || drive < 1.0f || drive > 8.0f)
            return false;
        drive_ = drive;
        return true;
    }

    float drive() const noexcept { return drive_; }
    void setEnabled(bool enabled) noexcept { enabled_ = enabled; }
    bool enabled() const noexcept { return enabled_; }

    float processSample(float input) noexcept override {
        if (!std::isfinite(input)) input = 0.0f;
        if (!enabled_) return input;
        const float driven = input * drive_;
        const float magnitude = std::fabs(driven);
        if (magnitude >= 1.0f) return driven < 0.0f ? -1.0f : 1.0f;
        // Odd cubic curve with unity small-signal slope and a zero-slope
        // transition at +/-1.0. It avoids a hard discontinuity at the knee.
        const float curved = magnitude + magnitude * magnitude -
                             magnitude * magnitude * magnitude;
        return driven < 0.0f ? -curved : curved;
    }

private:
    float drive_ = 1.0f;
    bool enabled_ = false;
};

// Fixed two-slot insert chain per track. Slot pointers are non-owning; no
// containers grow and no memory is allocated while processing audio.
class InsertChain {
public:
    static constexpr std::size_t kSlotCount = 2;

    bool setSlot(std::size_t slot, AudioInsert* effect) noexcept {
        if (slot >= kSlotCount) return false;
        effects_[slot] = effect;
        return true;
    }

    AudioInsert* slot(std::size_t index) const noexcept {
        return effects_[index < kSlotCount ? index : 0];
    }

    float processSample(float input) noexcept {
        float sample = std::isfinite(input) ? input : 0.0f;
        for (AudioInsert* effect : effects_) {
            if (effect != nullptr) sample = effect->processSample(sample);
        }
        return std::isfinite(sample) ? sample : 0.0f;
    }

private:
    std::array<AudioInsert*, kSlotCount> effects_{};
};

} // namespace mpcmixer
