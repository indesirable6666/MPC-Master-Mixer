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
