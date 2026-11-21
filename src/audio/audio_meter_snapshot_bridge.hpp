#pragma once

#include "audio_block_meter.hpp"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace mpcmixer {

// One complete meter frame produced at an audio block boundary. Indices
// [0, 15] are tracks and index 16 is the master bus.
struct AudioMeterSnapshot {
    static constexpr std::size_t kTrackCount = 16;
    static constexpr std::size_t kMasterIndex = kTrackCount;
    static constexpr std::size_t kChannelCount = kTrackCount + 1;

    std::uint64_t blockSequence = 0;
    std::array<AudioBlockMeterReading, kChannelCount> channels{};
};

// Bounded SPSC handoff from the audio callback (producer) to the UI/control
// thread (consumer). Full queues reject the newest frame; the audio thread
// never waits and never overwrites a slot still owned by the consumer.
class AudioMeterSnapshotBridge {
public:
    static constexpr std::size_t kQueueCapacity = 8;

    AudioMeterSnapshotBridge() = default;
    AudioMeterSnapshotBridge(const AudioMeterSnapshotBridge&) = delete;
    AudioMeterSnapshotBridge& operator=(const AudioMeterSnapshotBridge&) = delete;

    // Audio-thread producer only. Returns false when the queue is full.
    bool publish(const AudioMeterSnapshot& snapshot) noexcept {
        const std::uint32_t head = head_.load(std::memory_order_relaxed);
        const std::uint32_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= kQueueCapacity) {
            return false;
        }
        snapshots_[head % kQueueCapacity] = snapshot;
        head_.store(head + 1, std::memory_order_release);
        return true;
    }

    // UI/control-thread consumer only. Returns false when no frame is ready.
    bool consume(AudioMeterSnapshot& snapshot) noexcept {
        const std::uint32_t tail = tail_.load(std::memory_order_relaxed);
        const std::uint32_t head = head_.load(std::memory_order_acquire);
        if (tail == head) {
            return false;
        }
        snapshot = snapshots_[tail % kQueueCapacity];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    std::size_t pendingCountApprox() const noexcept {
        const std::uint32_t head = head_.load(std::memory_order_acquire);
        const std::uint32_t tail = tail_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(head - tail);
    }

private:
    // The producer writes a slot before publishing head_; the consumer reads
    // it after acquiring head_, then releases tail_ before the slot is reused.
    std::array<AudioMeterSnapshot, kQueueCapacity> snapshots_{};
    alignas(64) std::atomic<std::uint32_t> head_{0};
    alignas(64) std::atomic<std::uint32_t> tail_{0};
};

} // namespace mpcmixer
