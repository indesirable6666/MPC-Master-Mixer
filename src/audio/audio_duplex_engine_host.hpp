#pragma once

#include "audio_control_bridge.hpp"
#include "audio_duplex_backend.hpp"
#include "audio_meter_snapshot_bridge.hpp"

#include <cmath>
#include <cstdint>

#include <array>
#include <atomic>
#include <cstddef>

namespace mpcmixer {

// Connects a device-agnostic duplex callback to the mixer engine. Physical
// input-channel assignments are explicit: no MPC/ALSA channel order is assumed.
// Configure routing and prepare/start/stop the backend on the control thread.
class AudioDuplexEngineHost {
public:
    enum : std::size_t { kUnmappedTrack = AudioEngine::kTrackCount };

    explicit AudioDuplexEngineHost(
        AudioEngineCommandBridge& bridge,
        AudioMeterSnapshotBridge* meterBridge = nullptr) noexcept
        : bridge_(bridge), meterBridge_(meterBridge) {
        for (auto& route : inputTrack_)
            route.store(kUnmappedTrack, std::memory_order_relaxed);
    }

    AudioDuplexEngineHost(const AudioDuplexEngineHost&) = delete;
    AudioDuplexEngineHost& operator=(const AudioDuplexEngineHost&) = delete;

    bool setInputRoute(std::size_t inputChannel, std::size_t trackIndex) noexcept {
        if (inputChannel >= inputTrack_.size() ||
            (trackIndex != kUnmappedTrack && trackIndex >= AudioEngine::kTrackCount))
            return false;
        inputTrack_[inputChannel].store(trackIndex, std::memory_order_release);
        return true;
    }

    std::size_t inputRoute(std::size_t inputChannel) const noexcept {
        return inputChannel < inputTrack_.size()
            ? inputTrack_[inputChannel].load(std::memory_order_acquire)
            : kUnmappedTrack;
    }

    bool prepare(AudioDuplexBackend& backend, const AudioDuplexFormat& format) noexcept {
        if (backend_ != nullptr || !format.valid() || format.outputChannels != 2 ||
            format.inputChannels > inputTrack_.size()) return false;
        backend_ = &backend;
        if (!backend_->prepare(format, &AudioDuplexEngineHost::processCallback, this)) {
            backend_ = nullptr;
            return false;
        }
        expectedInputChannels_ = format.inputChannels;
        maxFramesPerBlock_ = format.maxFramesPerBlock;
        return true;
    }

    bool start() noexcept { return backend_ != nullptr && backend_->start(); }

    void stop() noexcept {
        if (backend_ != nullptr) backend_->stop();
    }

    void shutdown() noexcept {
        if (backend_ != nullptr) {
            backend_->stop();
            backend_->shutdown();
            backend_ = nullptr;
            expectedInputChannels_ = 0;
            maxFramesPerBlock_ = 0;
        }
    }

    // Public for deterministic offline tests and backend implementations that
    // prefer to invoke the host directly. All buffers are backend-owned.
    void processBlock(const AudioDuplexBuffer& block) noexcept {
        // The host cannot safely clear buffers when the callback exceeds the
        // prepared capacity: their actual allocation length is not knowable.
        if (maxFramesPerBlock_ == 0 || block.frameCount > maxFramesPerBlock_)
            return;
        if (block.outputChannels == nullptr)
            return;
        if (block.outputChannelCount != 2) {
            silence(block);
            return;
        }
        for (std::size_t channel = 0; channel < 2; ++channel) {
            if (block.outputChannels[channel] == nullptr) {
                silence(block);
                return;
            }
        }
        if (block.inputChannelCount != expectedInputChannels_ ||
            block.inputChannelCount > inputTrack_.size() ||
            (block.inputChannelCount != 0 && block.inputChannels == nullptr)) {
            silence(block);
            return;
        }

        std::array<const float*, AudioEngine::kTrackCount> trackInputs{};
        for (std::size_t channel = 0; channel < block.inputChannelCount; ++channel) {
            const std::size_t track =
                inputTrack_[channel].load(std::memory_order_acquire);
            if (track == kUnmappedTrack) continue;
            if (block.inputChannels[channel] == nullptr) {
                silence(block);
                return;
            }
            if (trackInputs[track] != nullptr) {
                silence(block); // Never replay stale output on invalid routing.
                return;
            }
            trackInputs[track] = block.inputChannels[channel];
        }
        bridge_.process(trackInputs.data(), block.outputChannels[0],
                        block.outputChannels[1], block.frameCount);

        // Metering is optional and remains a bounded audio-thread producer.
        // Track taps are the routed input signals; Master is post-mix output.
        if (meterBridge_ != nullptr && block.frameCount != 0) {
            AudioMeterSnapshot snapshot;
            snapshot.blockSequence = ++meterBlockSequence_;
            bool valid = true;
            for (std::size_t channel = 0; channel < block.inputChannelCount; ++channel) {
                const std::size_t track =
                    inputTrack_[channel].load(std::memory_order_acquire);
                if (track == kUnmappedTrack) continue;
                if (!AudioBlockMeter::measure(
                        block.inputChannels[channel], block.frameCount,
                        snapshot.channels[track])) {
                    valid = false;
                    break;
                }
            }

            AudioBlockMeterReading leftReading;
            AudioBlockMeterReading rightReading;
            if (valid &&
                AudioBlockMeter::measure(block.outputChannels[0], block.frameCount,
                                         leftReading) &&
                AudioBlockMeter::measure(block.outputChannels[1], block.frameCount,
                                         rightReading)) {
                AudioBlockMeterReading& master =
                    snapshot.channels[AudioMeterSnapshot::kMasterIndex];
                master.peak = leftReading.peak > rightReading.peak
                    ? leftReading.peak : rightReading.peak;
                master.rms = std::sqrt(
                    (leftReading.rms * leftReading.rms +
                     rightReading.rms * rightReading.rms) * 0.5f);
            } else {
                valid = false;
            }
            if (valid) {
                // A full meter queue drops this frame; never wait in the callback.
                (void)meterBridge_->publish(snapshot);
            }
        }
    }

    static void silence(const AudioDuplexBuffer& block) noexcept {
        if (block.outputChannels == nullptr) return;
        const std::size_t availableChannels =
            block.outputChannelCount < 2 ? block.outputChannelCount : 2;
        for (std::size_t channel = 0; channel < availableChannels; ++channel) {
            if (block.outputChannels[channel] == nullptr) continue;
            for (std::size_t frame = 0; frame < block.frameCount; ++frame)
                block.outputChannels[channel][frame] = 0.0f;
        }
    }

    static void processCallback(void* context, const AudioDuplexBuffer& block) {
        if (context != nullptr)
            static_cast<AudioDuplexEngineHost*>(context)->processBlock(block);
    }

private:
    AudioEngineCommandBridge& bridge_;
    AudioMeterSnapshotBridge* meterBridge_ = nullptr;
    std::uint64_t meterBlockSequence_ = 0;
    std::array<std::atomic<std::size_t>, 2> inputTrack_{};
    AudioDuplexBackend* backend_ = nullptr;
    std::size_t expectedInputChannels_ = 0;
    std::size_t maxFramesPerBlock_ = 0;
};

} // namespace mpcmixer
