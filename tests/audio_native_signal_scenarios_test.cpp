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
    format.maxFramesPerBlock = 8;
    assert(host.setInputRoute(0, 0));
    assert(host.prepare(backend, format));
    assert(host.start());

    const float input[] = {0.5f, -0.5f, 0.25f, -0.25f};
    const float* inputs[] = {input};
    float left[4]{};
    float right[4]{};
    float* outputs[] = {left, right};
    const mpcmixer::AudioDuplexBuffer block{inputs, outputs, 1, 2, 4};
    mpcmixer::AudioMeterSnapshot snapshot;

    // Fader at 50%: output follows track gain, while the track meter is
    // deliberately pre-fader, matching the host's documented meter tap.
    mpcmixer::AudioEngine::Track track;
    track.gain = 0.5f;
    assert(commands.enqueueTrack(0, track));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    const float centre = std::sqrt(0.5f);
    assert(near(left[0], 0.25f * centre));
    assert(near(right[0], left[0]));
    assert(meters.consume(snapshot));
    assert(near(snapshot.channels[0].peak, 0.5f));
    assert(near(snapshot.channels[0].rms, std::sqrt(0.15625f)));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak,
                0.25f * centre));

    // Hard-left pan sends no signal to the right output.
    track.pan = -1.0f;
    assert(commands.enqueueTrack(0, track));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(left[0], 0.25f));
    assert(near(right[0], 0.0f));
    assert(meters.consume(snapshot));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak,
                0.25f));

    // Track mute silences the main bus but does not falsify the pre-fader tap.
    track.mute = true;
    assert(commands.enqueueTrack(0, track));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    for (int i = 0; i < 4; ++i)
        assert(near(left[i], 0.0f) && near(right[i], 0.0f));
    assert(meters.consume(snapshot));
    assert(near(snapshot.channels[0].peak, 0.5f));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak,
                0.0f));

    // Restore the track and verify master gain and master mute independently.
    track.mute = false;
    track.pan = 0.0f;
    assert(commands.enqueueTrack(0, track));
    assert(commands.enqueueMasterGain(0.25f));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(left[0], 0.25f * 0.25f * centre));
    assert(near(right[0], left[0]));
    assert(meters.consume(snapshot));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak,
                0.25f * 0.25f * centre));

    assert(commands.enqueueMasterMute(true));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    for (int i = 0; i < 4; ++i)
        assert(near(left[i], 0.0f) && near(right[i], 0.0f));
    assert(meters.consume(snapshot));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak,
                0.0f));

    host.stop();
    host.shutdown();
    return 0;
}
