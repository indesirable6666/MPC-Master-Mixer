#include "../src/audio/audio_duplex_engine_host.hpp"

#include <cassert>
#include <cmath>
#include <limits>

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

    assert(host.setStereoInputPairEnabled(true));
    assert(host.stereoInputPairEnabled());
    assert(!host.setInputRoute(0, 3));
    assert(!host.setInputRoute(1, 4));

    mpcmixer::AudioDuplexFormat invalidFormat;
    invalidFormat.inputChannels = 1;
    assert(!host.prepare(backend, invalidFormat));
    assert(!host.start());

    mpcmixer::AudioDuplexFormat format;
    format.sampleRate = 48000;
    format.inputChannels = 2;
    format.outputChannels = 2;
    format.maxFramesPerBlock = 8;
    assert(host.prepare(backend, format));
    assert(!host.setStereoInputPairEnabled(false));
    assert(host.start());

    const float inputLeft[] = {1.0f, 0.5f, -0.5f};
    const float inputRight[] = {-0.25f, 0.75f, 0.5f};
    const float* inputs[] = {inputLeft, inputRight};
    float outputLeft[3]{};
    float outputRight[3]{};
    float* outputs[] = {outputLeft, outputRight};
    const mpcmixer::AudioDuplexBuffer block{inputs, outputs, 2, 2, 3};
    mpcmixer::AudioMeterSnapshot snapshot;

    mpcmixer::AudioInputPair::StereoControls stereo;
    stereo.gain = 0.5f;
    assert(commands.enqueueAudioInputPairLinked(true));
    assert(commands.enqueueAudioInputPairStereoControls(stereo));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.5f));
    assert(near(outputRight[0], -0.125f));
    assert(near(outputLeft[1], 0.25f));
    assert(near(outputRight[1], 0.375f));
    assert(meters.consume(snapshot));
    assert(snapshot.blockSequence == 1);
    assert(near(snapshot.channels[mpcmixer::AudioDuplexEngineHost::kAudioInputLeftTrackIndex].peak, 1.0f));
    assert(near(snapshot.channels[mpcmixer::AudioDuplexEngineHost::kAudioInputRightTrackIndex].peak, 0.75f));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak, 0.5f));

    // Linked balance attenuates only the opposite side and never collapses L/R.
    stereo.balance = 1.0f;
    assert(commands.enqueueAudioInputPairStereoControls(stereo));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.0f));
    assert(near(outputRight[0], -0.125f));
    assert(meters.consume(snapshot));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak, 0.375f));

    stereo.mute = true;
    assert(commands.enqueueAudioInputPairStereoControls(stereo));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    for (int i = 0; i < 3; ++i)
        assert(near(outputLeft[i], 0.0f) && near(outputRight[i], 0.0f));
    assert(meters.consume(snapshot));
    assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak, 0.0f));

    // Unlinked mode restores independent mono gain, pan, mute and solo.
    mpcmixer::AudioInputPair::Channel left;
    left.gain = 0.5f;
    left.pan = -1.0f;
    mpcmixer::AudioInputPair::Channel right;
    right.gain = 0.25f;
    right.pan = 1.0f;
    assert(commands.enqueueAudioInputPairLinked(false));
    assert(commands.enqueueAudioInputPairChannel(0, left));
    assert(commands.enqueueAudioInputPairChannel(1, right));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.5f));
    assert(near(outputRight[0], -0.0625f));
    assert(meters.consume(snapshot));

    left.solo = true;
    assert(commands.enqueueAudioInputPairChannel(0, left));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.5f));
    assert(near(outputRight[0], 0.0f));
    assert(meters.consume(snapshot));

    // Master gain and mute apply to the stereo pair as well as normal tracks.
    assert(commands.enqueueMasterGain(0.5f));
    assert(commands.enqueueMasterMute(false));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    assert(near(outputLeft[0], 0.25f));
    assert(near(outputRight[0], 0.0f));
    assert(meters.consume(snapshot));

    assert(commands.enqueueMasterMute(true));
    assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);
    for (int i = 0; i < 3; ++i)
        assert(near(outputLeft[i], 0.0f) && near(outputRight[i], 0.0f));
    assert(meters.consume(snapshot));

    mpcmixer::AudioInputPair::StereoControls invalidStereo;
    invalidStereo.balance = std::numeric_limits<float>::quiet_NaN();
    assert(!commands.enqueueAudioInputPairStereoControls(invalidStereo));
    mpcmixer::AudioInputPair::Channel invalidChannel;
    invalidChannel.gain = 1.28f;
    assert(!commands.enqueueAudioInputPairChannel(0, invalidChannel));
    assert(!commands.enqueueAudioInputPairChannel(2, left));

    host.stop();
    host.shutdown();
    assert(host.setStereoInputPairEnabled(false));
    assert(!host.stereoInputPairEnabled());
    assert(host.setInputRoute(0, 3));
    assert(host.inputRoute(0) == 3);
    return 0;
}
