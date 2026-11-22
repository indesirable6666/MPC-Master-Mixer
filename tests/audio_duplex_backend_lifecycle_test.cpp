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
        if (!prepared) return false;
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

    // Releasing a prepared host must always be an explicit lifecycle step.
    return 0;
}
