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
    enum : std::size_t {
        kUnmappedTrack = AudioEngine::kTrackCount,
        // These meter slots match the prototype's Audio In 1/2 strips.
        kAudioInputLeftTrackIndex = 13,
        kAudioInputRightTrackIndex = 14,
        kFirstAuxReturnInputChannel = 2
    };

    explicit AudioDuplexEngineHost(
        AudioEngineCommandBridge& bridge,
        AudioMeterSnapshotBridge* meterBridge = nullptr) noexcept
        : bridge_(bridge), meterBridge_(meterBridge) {
        for (auto& route : inputTrack_)
            route.store(kUnmappedTrack, std::memory_order_relaxed);
    }

    // The backend must outlive this host. Destruction stops callbacks and
    // releases backend resources if shutdown() was not called explicitly.
    ~AudioDuplexEngineHost() noexcept { shutdown(); }

    AudioDuplexEngineHost(const AudioDuplexEngineHost&) = delete;
    AudioDuplexEngineHost& operator=(const AudioDuplexEngineHost&) = delete;

    bool setInputRoute(std::size_t inputChannel, std::size_t trackIndex) noexcept {
        if (inputChannel >= inputTrack_.size() ||
            (stereoInputPairEnabled_ && inputChannel < 2) ||
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

    // Opt-in stereo Audio In consumes source input channels 0/1 as one
    // linked/unlinked pair. Optional Aux Return pairs may follow on channels
    // 2..9; the source pair and per-track routes are mutually exclusive.
    bool setStereoInputPairEnabled(bool enabled) noexcept {
        if (backend_ != nullptr) return false;
        if (enabled && (inputRoute(0) != kUnmappedTrack ||
                        inputRoute(1) != kUnmappedTrack)) return false;
        stereoInputPairEnabled_ = enabled;
        return true;
    }

    bool stereoInputPairEnabled() const noexcept {
        return stereoInputPairEnabled_;
    }

    // Input channels 0/1 are source inputs (stereo Audio In or independently
    // routed mono tracks). Optional Aux Returns follow as complete stereo pairs:
    // Aux 1 L/R at 2/3, Aux 2 at 4/5, Aux 3 at 6/7, and Aux 4 at 8/9.
    // Output layout is logical and device-neutral: Main L/R occupy channels
    // 0/1, followed by up to four stereo Aux buses at 2+2*bus / 3+2*bus.
    // A format may expose the main pair alone or a prefix of complete Aux pairs.
    bool prepare(AudioDuplexBackend& backend, const AudioDuplexFormat& format) noexcept {
        const bool validInputLayout = format.inputChannels <= 2 ||
            (format.inputChannels >= 4 && (format.inputChannels % 2) == 0);
        if (backend_ != nullptr || !format.valid() || !validInputLayout ||
            format.outputChannels < 2 || (format.outputChannels % 2) != 0 ||
            (stereoInputPairEnabled_ && format.inputChannels < 2)) return false;
        backend_ = &backend;
        if (!backend_->prepare(format, &AudioDuplexEngineHost::processCallback, this)) {
            backend_ = nullptr;
            return false;
        }
        expectedInputChannels_ = format.inputChannels;
        expectedOutputChannels_ = format.outputChannels;
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
            expectedOutputChannels_ = 0;
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
        if (block.outputChannelCount != expectedOutputChannels_ ||
            block.outputChannelCount < 2 ||
            block.outputChannelCount > AudioDuplexFormat::kMaxOutputChannels) {
            silence(block);
            return;
        }
        for (std::size_t channel = 0; channel < expectedOutputChannels_; ++channel) {
            if (block.outputChannels[channel] == nullptr) {
                silence(block);
                return;
            }
        }
        if (block.inputChannelCount != expectedInputChannels_ ||
            block.inputChannelCount > AudioDuplexFormat::kMaxInputChannels ||
            (block.inputChannelCount != 0 && block.inputChannels == nullptr)) {
            silence(block);
            return;
        }

        const std::size_t sourceInputChannels =
            block.inputChannelCount < 2 ? block.inputChannelCount : 2;
        const std::size_t availableAuxReturns =
            block.inputChannelCount > kFirstAuxReturnInputChannel
                ? (block.inputChannelCount - kFirstAuxReturnInputChannel) / 2
                : 0;
        std::array<const float*, AudioEngine::kAuxBusCount> returnLeft{};
        std::array<const float*, AudioEngine::kAuxBusCount> returnRight{};
        for (std::size_t bus = 0; bus < availableAuxReturns; ++bus) {
            const std::size_t leftChannel = kFirstAuxReturnInputChannel + bus * 2;
            returnLeft[bus] = block.inputChannels[leftChannel];
            returnRight[bus] = block.inputChannels[leftChannel + 1];
            if (returnLeft[bus] == nullptr || returnRight[bus] == nullptr) {
                silence(block);
                return;
            }
        }
        const float* const* auxReturnLeftInputs =
            availableAuxReturns == 0 ? nullptr : returnLeft.data();
        const float* const* auxReturnRightInputs =
            availableAuxReturns == 0 ? nullptr : returnRight.data();

        std::array<const float*, AudioEngine::kTrackCount> trackInputs{};
        std::array<std::size_t, 2> blockRoutes{{kUnmappedTrack, kUnmappedTrack}};
        std::array<float*, AudioEngine::kAuxBusCount> auxLeft{};
        std::array<float*, AudioEngine::kAuxBusCount> auxRight{};
        const std::size_t availableAuxBuses = (block.outputChannelCount - 2) / 2;
        for (std::size_t bus = 0; bus < availableAuxBuses; ++bus) {
            auxLeft[bus] = block.outputChannels[2 + bus * 2];
            auxRight[bus] = block.outputChannels[3 + bus * 2];
        }
        float* const* auxLeftOutputs =
            availableAuxBuses == 0 ? nullptr : auxLeft.data();
        float* const* auxRightOutputs =
            availableAuxBuses == 0 ? nullptr : auxRight.data();
        if (stereoInputPairEnabled_) {
            if (block.inputChannelCount < 2 ||
                block.inputChannels[0] == nullptr ||
                block.inputChannels[1] == nullptr) {
                silence(block);
                return;
            }
            bridge_.processWithAudioInputPair(
                trackInputs.data(), block.inputChannels[0], block.inputChannels[1],
                block.outputChannels[0], block.outputChannels[1], block.frameCount,
                auxLeftOutputs, auxRightOutputs,
                auxReturnLeftInputs, auxReturnRightInputs);
        } else {
            for (std::size_t channel = 0; channel < sourceInputChannels; ++channel) {
                const std::size_t track =
                    inputTrack_[channel].load(std::memory_order_acquire);
                blockRoutes[channel] = track;
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
            if (availableAuxBuses != 0 || availableAuxReturns != 0) {
                bridge_.processWithAuxAndReturns(
                    trackInputs.data(), block.outputChannels[0],
                    block.outputChannels[1], auxLeftOutputs, auxRightOutputs,
                    auxReturnLeftInputs, auxReturnRightInputs, block.frameCount);
            } else {
                bridge_.process(trackInputs.data(), block.outputChannels[0],
                                block.outputChannels[1], block.frameCount);
            }
        }

        // Metering is optional and remains a bounded audio-thread producer.
        // Pair taps are pre-control Audio In signals; Master is post-mix output.
        if (meterBridge_ != nullptr && block.frameCount != 0) {
            AudioMeterSnapshot snapshot;
            snapshot.blockSequence = ++meterBlockSequence_;
            bool valid = true;
            if (stereoInputPairEnabled_) {
                valid = AudioBlockMeter::measure(
                    block.inputChannels[0], block.frameCount,
                    snapshot.channels[kAudioInputLeftTrackIndex]) &&
                    AudioBlockMeter::measure(
                    block.inputChannels[1], block.frameCount,
                    snapshot.channels[kAudioInputRightTrackIndex]);
            } else {
                for (std::size_t channel = 0; channel < sourceInputChannels; ++channel) {
                    const std::size_t track = blockRoutes[channel];
                    if (track == kUnmappedTrack) continue;
                    if (!AudioBlockMeter::measure(
                            block.inputChannels[channel], block.frameCount,
                            snapshot.channels[track])) {
                        valid = false;
                        break;
                    }
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
        std::size_t availableChannels =
            block.outputChannelCount < AudioDuplexFormat::kMaxOutputChannels
                ? block.outputChannelCount
                : static_cast<std::size_t>(AudioDuplexFormat::kMaxOutputChannels);
        // Never touch an orphaned Aux plane from a malformed odd-sized layout;
        // a single supplied plane is still cleared as a partial main pair.
        if (availableChannels > 2 && (availableChannels % 2) != 0)
            --availableChannels;
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
    std::size_t expectedOutputChannels_ = 0;
    std::size_t maxFramesPerBlock_ = 0;
    bool stereoInputPairEnabled_ = false;
};

} // namespace mpcmixer
