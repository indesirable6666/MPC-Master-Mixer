#include "../src/audio/audio_duplex_engine_host.hpp"

#include <cassert>
#include <cstddef>

namespace {
class RecordingBackend final : public mpcmixer::AudioDuplexBackend {
public:
    bool prepare(const mpcmixer::AudioDuplexFormat& format,
                 mpcmixer::AudioProcessCallback callback,
                 void* context) noexcept override {
        ++prepareCalls;
        if (failPrepare || prepared || callback == nullptr || context == nullptr)
            return false;
        format_ = format;
        callback_ = callback;
        context_ = context;
        prepared = true;
        running = false;
        return true;
    }

    bool start() noexcept override {
        ++startCalls;
        if (!prepared || failStart) return false;
        running = true;
        return true;
    }

    void stop() noexcept override {
        ++stopCalls;
        running = false;
    }

    void shutdown() noexcept override {
        ++shutdownCalls;
        running = false;
        prepared = false;
        callback_ = nullptr;
        context_ = nullptr;
    }

    bool emit(const mpcmixer::AudioDuplexBuffer& block) noexcept {
        if (!running || callback_ == nullptr) return false;
        callback_(context_, block);
        return true;
    }

    bool failPrepare = false;
    bool failStart = false;
    bool prepared = false;
    bool running = false;
    unsigned prepareCalls = 0;
    unsigned startCalls = 0;
    unsigned stopCalls = 0;
    unsigned shutdownCalls = 0;

private:
    mpcmixer::AudioDuplexFormat format_{};
    mpcmixer::AudioProcessCallback callback_ = nullptr;
    void* context_ = nullptr;
};
} // namespace

int main() {
    using namespace mpcmixer;

    AudioEngineCommandBridge bridge;
    AudioDuplexEngineHost host(bridge);
    RecordingBackend backend;

    AudioDuplexFormat format;
    format.inputChannels = 0;
    format.outputChannels = 2;
    format.maxFramesPerBlock = 8;

    assert(!host.start());
    backend.failPrepare = true;
    assert(!host.prepare(backend, format));
    assert(!backend.prepared);
    assert(!host.start());

    // A failed prepare leaves the host reusable and does not retain the backend.
    backend.failPrepare = false;
    assert(host.prepare(backend, format));
    assert(!host.prepare(backend, format));

    // A failed start preserves the prepared configuration but does not run.
    backend.failStart = true;
    assert(!host.start());
    assert(backend.prepared);
    assert(!backend.running);
    backend.failStart = false;
    assert(host.start());

    float left[2] = {1.0f, 1.0f};
    float right[2] = {1.0f, 1.0f};
    float* outputs[2] = {left, right};
    AudioDuplexBuffer block;
    block.outputChannels = outputs;
    block.outputChannelCount = 2;
    block.frameCount = 2;

    assert(backend.emit(block));
    assert(left[0] == 0.0f && left[1] == 0.0f);
    assert(right[0] == 0.0f && right[1] == 0.0f);

    host.stop();
    assert(!backend.emit(block));
    assert(host.start());
    assert(backend.emit(block));

    host.shutdown();
    assert(!backend.prepared);
    assert(!backend.running);
    assert(!host.start());
    assert(backend.stopCalls == 2); // Explicit stop and shutdown's safety stop.
    assert(backend.shutdownCalls == 1);

    // RAII shutdown also stops callbacks when the caller omits shutdown().
    // Declare the backend first so it outlives the host, as required by contract.
    RecordingBackend scopedBackend;
    {
        AudioDuplexEngineHost scopedHost(bridge);
        assert(scopedHost.prepare(scopedBackend, format));
        assert(scopedHost.start());
    }
    assert(!scopedBackend.running);
    assert(!scopedBackend.prepared);
    assert(scopedBackend.stopCalls == 1);
    assert(scopedBackend.shutdownCalls == 1);

    // The offline driver follows the same lifecycle contract as a future
    // device backend, without opening or probing any hardware.
    struct OfflineProbe {
        unsigned calls = 0;
    };
    const auto copyOfflineBlock = [](void* context,
                                    const AudioDuplexBuffer& block) {
        OfflineProbe* probe = static_cast<OfflineProbe*>(context);
        ++probe->calls;
        for (std::size_t frame = 0; frame < block.frameCount; ++frame) {
            block.outputChannels[0][frame] = block.inputChannels[0][frame];
            block.outputChannels[1][frame] = block.inputChannels[1][frame];
        }
    };

    OfflineAudioDuplexBackend offline;
    AudioDuplexFormat offlineFormat;
    offlineFormat.maxFramesPerBlock = 4;
    AudioDuplexFormat invalidOfflineFormat = offlineFormat;
    invalidOfflineFormat.sampleRate = 0;
    OfflineProbe offlineProbe;
    assert(!offline.start());
    assert(!offline.prepare(invalidOfflineFormat, copyOfflineBlock, &offlineProbe));
    assert(!offline.start());
    assert(offline.prepare(offlineFormat, copyOfflineBlock, &offlineProbe));
    assert(offline.start());

    const float offlineInputLeft[] = {0.25f, -0.5f};
    const float offlineInputRight[] = {-0.75f, 1.0f};
    const float* offlineInputs[] = {offlineInputLeft, offlineInputRight};
    float offlineOutputLeft[2] = {};
    float offlineOutputRight[2] = {};
    float* offlineOutputs[] = {offlineOutputLeft, offlineOutputRight};
    const AudioDuplexBuffer offlineBlock{
        offlineInputs, offlineOutputs, 2, 2, 2
    };
    assert(offline.processBlock(offlineBlock) == AudioDuplexStatus::Processed);
    assert(offlineProbe.calls == 1);
    assert(offlineOutputLeft[1] == -0.5f);
    assert(offlineOutputRight[1] == 1.0f);

    // A second prepare is rejected without tearing down the active stream.
    AudioDuplexFormat replacementFormat = offlineFormat;
    replacementFormat.sampleRate = 96000;
    assert(!offline.prepare(replacementFormat, copyOfflineBlock, &offlineProbe));
    assert(offline.processBlock(offlineBlock) == AudioDuplexStatus::Processed);
    assert(offlineProbe.calls == 2);

    offline.stop();
    assert(offline.processBlock(offlineBlock) == AudioDuplexStatus::NotPrepared);
    assert(offline.start()); // stop retains the prepared configuration.
    assert(offline.processBlock(offlineBlock) == AudioDuplexStatus::Processed);
    assert(offlineProbe.calls == 3);
    offline.shutdown();
    assert(!offline.start());
    assert(offline.processBlock(offlineBlock) == AudioDuplexStatus::NotPrepared);

    // shutdown() releases the previous configuration and permits re-prepare.
    assert(offline.prepare(replacementFormat, copyOfflineBlock, &offlineProbe));
    assert(offline.start());
    assert(offline.processBlock(offlineBlock) == AudioDuplexStatus::Processed);
    assert(offlineProbe.calls == 4);
    offline.shutdown();

    return 0;
}
