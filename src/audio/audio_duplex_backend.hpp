#pragma once
#include <cstddef>
namespace mpcmixer {
struct AudioDuplexFormat {
    enum { kMaxInputChannels = 10, kMaxOutputChannels = 10 };

    unsigned sampleRate = 48000;
    std::size_t inputChannels = 2, outputChannels = 2, maxFramesPerBlock = 512;
    bool valid() const noexcept {
        return sampleRate >= 8000 && sampleRate <= 192000 &&
            inputChannels <= kMaxInputChannels && outputChannels > 0 &&
            outputChannels <= kMaxOutputChannels &&
            maxFramesPerBlock > 0 && maxFramesPerBlock <= 8192;
    }
};
struct AudioDuplexBuffer {
    const float* const* inputChannels = nullptr;
    float* const* outputChannels = nullptr;
    std::size_t inputChannelCount = 0, outputChannelCount = 0, frameCount = 0;
};
enum class AudioDuplexStatus { Processed, NotPrepared, InvalidBuffer };
using AudioProcessCallback = void (*)(void*, const AudioDuplexBuffer&);
// Lifecycle methods run on the control thread. A failed prepare() must leave the
// backend stopped and unprepared, release any partial resources, and guarantee
// that no callback will be made. After a successful prepare(), stop() must
// prevent further callbacks before it returns; shutdown() releases prepared
// resources and is safe after stop(). Implementations must invoke the callback
// with backend-owned buffers without allocation, locks, logging or waits.
class AudioDuplexBackend {
public:
    virtual ~AudioDuplexBackend() = default;
    virtual bool prepare(const AudioDuplexFormat&, AudioProcessCallback, void*) noexcept = 0;
    virtual bool start() noexcept = 0;
    virtual void stop() noexcept = 0;
    virtual void shutdown() noexcept = 0;
};
// Deterministic offline callback driver; it never opens or configures a device.
class OfflineAudioDuplexBackend final : public AudioDuplexBackend {
public:
    bool prepare(const AudioDuplexFormat& f, AudioProcessCallback cb, void* ctx) noexcept override {
        if (prepared_ || !f.valid() || cb == nullptr) return false;
        format_ = f; callback_ = cb; context_ = ctx; prepared_ = true; running_ = false;
        return true;
    }
    bool start() noexcept override {
        if (!prepared_) return false;
        running_ = true; return true;
    }
    AudioDuplexStatus processBlock(const AudioDuplexBuffer& b) noexcept {
        if (!prepared_ || !running_) return AudioDuplexStatus::NotPrepared;
        if (b.frameCount > format_.maxFramesPerBlock ||
            b.inputChannelCount != format_.inputChannels ||
            b.outputChannelCount != format_.outputChannels ||
            b.outputChannels == nullptr ||
            (format_.inputChannels != 0 && b.inputChannels == nullptr))
            return AudioDuplexStatus::InvalidBuffer;
        for (std::size_t i = 0; i < b.outputChannelCount; ++i)
            if (b.outputChannels[i] == nullptr) return AudioDuplexStatus::InvalidBuffer;
        for (std::size_t i = 0; i < b.inputChannelCount; ++i)
            if (b.inputChannels[i] == nullptr) return AudioDuplexStatus::InvalidBuffer;
        callback_(context_, b);
        return AudioDuplexStatus::Processed;
    }
    void stop() noexcept override { running_ = false; }
    void shutdown() noexcept override {
        running_ = prepared_ = false; callback_ = nullptr; context_ = nullptr;
        format_ = AudioDuplexFormat{};
    }
private:
    AudioDuplexFormat format_{};
    AudioProcessCallback callback_ = nullptr;
    void* context_ = nullptr;
    bool prepared_ = false, running_ = false;
};
} // namespace mpcmixer
