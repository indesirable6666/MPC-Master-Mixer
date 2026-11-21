#include "../src/audio/audio_duplex_engine_host.hpp"
#include "../src/mixer/meter_presentation_adapter.hpp"
#include "../src/mixer/mixer_controller.hpp"

#include <cassert>
#include <cmath>
#include <vector>

namespace {
class FakeSink final : public mpcmixer::MeterPresentationSink {
public:
    void present(const mpcmixer::MeterPresentationFrame& frame) override {
        frames.push_back(frame);
    }
    std::vector<mpcmixer::MeterPresentationFrame> frames;
};

bool near(float actual, float expected, float tolerance = 0.0001f) {
    return std::fabs(actual - expected) <= tolerance;
}
}

int main() {
    mpcmixer::AudioEngineCommandBridge commands;
    mpcmixer::AudioMeterSnapshotBridge meterBridge;
    mpcmixer::AudioDuplexEngineHost host(commands, &meterBridge);
    mpcmixer::OfflineAudioDuplexBackend backend;
    mpcmixer::AudioMeterUiConsumer consumer(meterBridge, 2.0f);
    FakeSink sink;
    mpcmixer::MeterPresentationAdapter presentation(consumer, sink);
    mpcmixer::MixerController controller(commands);

    mpcmixer::AudioDuplexFormat format;
    format.sampleRate = 48000;
    format.inputChannels = 1;
    format.outputChannels = 2;
    format.maxFramesPerBlock = 16;

    assert(host.setInputRoute(0, 0));
    assert(host.prepare(backend, format));
    assert(host.start());

    // A UI control update travels through the command queue and is applied
    // at the next audio block boundary. Track metering remains pre-fader.
    assert(controller.setTrackFader(0, 50));

    const float input[] = {0.5f, -0.5f, 0.25f, -0.25f};
    const float* inputs[] = {input};
    float left[4]{};
    float right[4]{};
    float* outputs[] = {left, right};
    const mpcmixer::AudioDuplexBuffer block{inputs, outputs, 1, 2, 4};

    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(left[0], 0.5f * 0.5f * std::sqrt(0.5f)));
    assert(near(right[0], left[0]));

    // The presentation adapter consumes the audio snapshot, updates the
    // UI-owned meter models and emits one copied 17-channel frame.
    assert(presentation.tick(0.0f));
    assert(sink.frames.size() == 1);
    const auto& first = sink.frames.back();
    assert(first.sequence == 1);
    assert(first.sourceBlockSequence == 1);
    assert(near(first.channels[0].peak, 0.5f));
    assert(near(first.channels[0].rms, std::sqrt(0.15625f)));

    const float expectedMasterPeak = 0.5f * 0.5f * std::sqrt(0.5f);
    const float expectedMasterRms =
        std::sqrt(0.15625f) * 0.5f * std::sqrt(0.5f);
    const auto master = mpcmixer::AudioMeterSnapshot::kMasterIndex;
    assert(near(first.channels[master].peak, expectedMasterPeak));
    assert(near(first.channels[master].rms, expectedMasterRms));

    // A later UI tick advances presentation sequence while keeping the same
    // source block sequence; peak decay is applied on the UI side only.
    assert(presentation.tick(0.1f));
    assert(sink.frames.size() == 2);
    assert(sink.frames.back().sequence == 2);
    assert(sink.frames.back().sourceBlockSequence == 1);
    assert(near(sink.frames.back().channels[0].peak, 0.3f));
    assert(near(sink.frames.back().channels[0].rms, std::sqrt(0.15625f)));

    host.stop();
    host.shutdown();
    return 0;
}
