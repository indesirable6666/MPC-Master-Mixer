#pragma once

#include <algorithm>
#include <cstddef>

namespace mpcmixer {

// Describes caller-owned planar float capture buffers. A backend must accept
// only formats it can provide without changing the host audio configuration.
struct AudioFormat {
    unsigned sampleRate = 48000;
    std::size_t channelCount = 2;
    std::size_t maxFramesPerBlock = 512;

    bool valid() const {
        return sampleRate >= 8000 && sampleRate <= 192000 &&
            (channelCount == 1 || channelCount == 2) &&
            maxFramesPerBlock > 0 && maxFramesPerBlock <= 8192;
    }
};

enum class CaptureStatus {
    Captured,
    Silence,
    NotPrepared,
    InvalidBuffer
};

// Device I/O boundary, deliberately separate from the DSP engine.
// prepare()/shutdown() belong on a control thread. capture() is called by the
// audio thread and implementations must not allocate, lock, log, or block.
// Buffer ownership always remains with the caller.
class AudioCaptureBackend {
public:
    virtual ~AudioCaptureBackend() = default;

    virtual bool prepare(const AudioFormat& format) noexcept = 0;
    virtual CaptureStatus capture(float* const* outputChannels,
                                  std::size_t channelCount,
                                  std::size_t frameCount) noexcept = 0;
    virtual void shutdown() noexcept = 0;
};

// Safe default backend for offline UI and DSP development. It never opens a
// device and always supplies explicit silence, so callers cannot mistake an
// unconnected input for captured audio.
class SilentAudioCaptureBackend final : public AudioCaptureBackend {
public:
    bool prepare(const AudioFormat& format) noexcept override {
        if (!format.valid()) return false;
        format_ = format;
        prepared_ = true;
        return true;
    }

    CaptureStatus capture(float* const* outputChannels,
                          std::size_t channelCount,
                          std::size_t frameCount) noexcept override {
        if (!prepared_) return CaptureStatus::NotPrepared;
        if (outputChannels == nullptr ||
            channelCount != format_.channelCount ||
            frameCount > format_.maxFramesPerBlock) {
            return CaptureStatus::InvalidBuffer;
        }
        for (std::size_t channel = 0; channel < channelCount; ++channel) {
            if (outputChannels[channel] == nullptr)
                return CaptureStatus::InvalidBuffer;
        }
        for (std::size_t channel = 0; channel < channelCount; ++channel) {
            std::fill_n(outputChannels[channel], frameCount, 0.0f);
        }
        return CaptureStatus::Silence;
    }

    void shutdown() noexcept override {
        prepared_ = false;
        format_ = AudioFormat{};
    }

private:
    AudioFormat format_{};
    bool prepared_ = false;
};

} // namespace mpcmixer
