#include "../src/audio/audio_duplex_engine_host.hpp"

#include <cassert>
#include <cmath>
#include <fstream>
#include <sstream>
#include <string>

namespace {
bool near(float actual, float expected, float tolerance = 0.0002f) {
    return std::fabs(actual - expected) <= tolerance;
}
}

int main() {
    std::ifstream fixture("tests/fixtures/audio_signal_reference.csv");
    assert(fixture && "shared browser/native audio fixture must be available");
    std::string line;
    assert(std::getline(fixture, line)); // header
    unsigned scenarios = 0;

    while (std::getline(fixture, line)) {
        if (line.empty()) continue;
        std::istringstream row(line);
        std::string name;
        float trackGain = 0.0f, pan = 0.0f, masterGain = 0.0f;
        int trackMute = 0, masterMute = 0;
        float expectedLeftGain = 0.0f, expectedRightGain = 0.0f;
        float expectedMasterPeak = 0.0f;
        char comma = ',';
        assert(std::getline(row, name, ','));
        assert(row >> trackGain >> comma && comma == ',');
        assert(row >> pan >> comma && comma == ',');
        assert(row >> masterGain >> comma && comma == ',');
        assert(row >> trackMute >> comma && comma == ',');
        assert(row >> masterMute >> comma && comma == ',');
        assert(row >> expectedLeftGain >> comma && comma == ',');
        assert(row >> expectedRightGain >> comma && comma == ',');
        assert(row >> expectedMasterPeak);

        mpcmixer::AudioEngineCommandBridge commands;
        mpcmixer::AudioMeterSnapshotBridge meters;
        mpcmixer::AudioDuplexEngineHost host(commands, &meters);
        mpcmixer::OfflineAudioDuplexBackend backend;
        mpcmixer::AudioDuplexFormat format;
        format.sampleRate = 48000;
        format.inputChannels = 1;
        format.outputChannels = 2;
        format.maxFramesPerBlock = 4;
        assert(host.setInputRoute(0, 0));
        assert(host.prepare(backend, format));
        assert(host.start());

        const float input[] = {0.5f, -0.5f, 0.25f, -0.25f};
        const float* inputs[] = {input};
        float left[4]{};
        float right[4]{};
        float* outputs[] = {left, right};
        const mpcmixer::AudioDuplexBuffer block{inputs, outputs, 1, 2, 4};

        mpcmixer::AudioEngine::Track track;
        track.gain = trackGain;
        track.pan = pan;
        track.mute = trackMute != 0;
        assert(commands.enqueueTrack(0, track));
        assert(commands.enqueueMasterGain(masterGain));
        assert(commands.enqueueMasterMute(masterMute != 0));
        assert(backend.processBlock(block) == mpcmixer::AudioDuplexStatus::Processed);

        assert(near(left[0] / input[0], expectedLeftGain));
        assert(near(right[0] / input[0], expectedRightGain));
        assert(near(std::fabs(left[0]) > std::fabs(right[0])
                        ? std::fabs(left[0]) : std::fabs(right[0]),
                    expectedMasterPeak));
        mpcmixer::AudioMeterSnapshot snapshot;
        assert(meters.consume(snapshot));
        assert(near(snapshot.channels[0].peak, 0.5f));
        assert(near(snapshot.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak,
                    expectedMasterPeak));

        host.stop();
        host.shutdown();
        ++scenarios;
    }
    assert(scenarios == 5);
    return 0;
}
