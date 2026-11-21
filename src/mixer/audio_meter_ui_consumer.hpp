#pragma once

#include "../audio/audio_meter_snapshot_bridge.hpp"
#include "meter_model.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mpcmixer {

// UI-thread-only adapter: drains audio snapshots and updates 16 track meters
// plus the master. Never call from the audio callback or from multiple threads.
class AudioMeterUiConsumer {
public:
    static constexpr std::size_t kChannelCount =
        AudioMeterSnapshot::kChannelCount;
    static constexpr float kDefaultPeakDecayPerSecond = 2.5f;

    explicit AudioMeterUiConsumer(
        AudioMeterSnapshotBridge& bridge,
        float peakDecayPerSecond = kDefaultPeakDecayPerSecond) noexcept
        : bridge_(bridge),
          peakDecayPerSecond_(std::isfinite(peakDecayPerSecond) &&
                                      peakDecayPerSecond >= 0.0f
                                  ? peakDecayPerSecond
                                  : kDefaultPeakDecayPerSecond) {}

    // Call from the UI timer/render loop. Elapsed time is in seconds.
    // All queued frames are drained in order: peak hold observes every frame,
    // while RMS naturally ends at the newest valid frame.
    // Peak decay is time-based, so refresh-rate changes do not change its speed.
    bool tick(float elapsedSeconds) noexcept {
        if (std::isfinite(elapsedSeconds) && elapsedSeconds > 0.0f) {
            const float decay = elapsedSeconds * peakDecayPerSecond_;
            for (auto& meter : meters_) meter.decayPeak(decay);
        }

        AudioMeterSnapshot snapshot;
        bool received = false;
        while (bridge_.consume(snapshot)) {
            if (hasSnapshot_ && snapshot.blockSequence <= lastSequence_) {
                continue;
            }
            for (std::size_t i = 0; i < kChannelCount; ++i) {
                meters_[i].update(snapshot.channels[i].peak,
                                  snapshot.channels[i].rms);
            }
            lastSequence_ = snapshot.blockSequence;
            hasSnapshot_ = true;
            received = true;
        }
        return received;
    }

    // Clip reset is UI-thread-only, like tick(). Returns false for an invalid channel.
    bool resetClip(std::size_t channel) noexcept {
        if (channel >= kChannelCount) return false;
        meters_[channel].resetClip();
        return true;
    }

    void resetAllClips() noexcept {
        for (auto& meter : meters_) meter.resetClip();
    }

    const MeterModel& meter(std::size_t channel) const noexcept {
        return meters_[channel < kChannelCount ? channel : 0];
    }

    bool hasSnapshot() const noexcept { return hasSnapshot_; }
    std::uint64_t lastSequence() const noexcept { return lastSequence_; }

private:
    AudioMeterSnapshotBridge& bridge_;
    float peakDecayPerSecond_;
    std::array<MeterModel, kChannelCount> meters_{};
    std::uint64_t lastSequence_ = 0;
    bool hasSnapshot_ = false;
};

} // namespace mpcmixer
