#include "../src/audio/audio_duplex_engine_host.hpp"

#include <cassert>
#include <cmath>

namespace {
bool near(float actual, float expected, float tolerance = 0.0001f) {
    return std::fabs(actual - expected) <= tolerance;
}
}

int main() {
    mpcmixer::AudioEngineCommandBridge commands;
    mpcmixer::AudioMeterSnapshotBridge meters;
    mpcmixer::AudioDuplexEngineHost host(commands, &meters);
    mpcmixer::OfflineAudioDuplexBackend backend;

    mpcmixer::AudioDuplexFormat format;
    format.sampleRate = 48000;
    format.inputChannels = 1;
    format.outputChannels = 2;
    format.maxFramesPerBlock = 16;
    assert(host.setInputRoute(0, 0));
    assert(host.prepare(backend, format));
    assert(host.start());

    const float input[] = {0.5f, -0.5f, 0.25f, -0.25f};
    const float* inputs[] = {input};
    float left[4]{};
    float right[4]{};
    float* outputs[] = {left, right};
    const mpcmixer::AudioDuplexBuffer block{inputs, outputs, 1, 2, 4};
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);

    mpcmixer::AudioMeterSnapshot snapshot;
    assert(meters.consume(snapshot));
    assert(snapshot.blockSequence == 1);
    assert(near(snapshot.channels[0].peak, 0.5f));
    assert(near(snapshot.channels[0].rms, std::sqrt(0.15625f)));

    // Master is measured after mixing. The centered default pan feeds both
    // outputs with equal-power gain, so combined stereo RMS is predictable.
    const float expectedMasterRms = std::sqrt(0.15625f) * std::sqrt(0.5f);
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak,
                0.5f * std::sqrt(0.5f)));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].rms,
                expectedMasterRms));
    assert(!meters.consume(snapshot));

    // A second block increments the sequence and does not reuse stale data.
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(meters.consume(snapshot));
    assert(snapshot.blockSequence == 2);

    host.stop();
    host.shutdown();
    return 0;
}
