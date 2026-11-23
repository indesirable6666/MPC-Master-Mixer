#include "../src/audio/audio_duplex_engine_host.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
bool near(float actual, float expected, float tolerance = 0.00001f) {
    return std::fabs(actual - expected) <= tolerance;
}
}

int main() {
    using namespace mpcmixer;

    AudioEngineCommandBridge bridge;
    AudioDuplexEngineHost host(bridge);
    OfflineAudioDuplexBackend backend;

    assert(host.setStereoInputPairEnabled(true));
    AudioDuplexFormat format;
    format.inputChannels = 2;
    format.outputChannels = 3; // A main pair plus an orphaned plane is invalid.
    format.maxFramesPerBlock = 4;
    assert(format.valid());
    assert(!host.prepare(backend, format));
    format.outputChannels = AudioDuplexFormat::kMaxOutputChannels + 2;
    assert(!format.valid());
    format.outputChannels = 10; // Main L/R followed by four stereo Aux pairs.
    assert(format.valid());
    assert(host.prepare(backend, format));
    assert(host.start());

    const float inputLeft[] = {0.5f, -0.25f};
    const float inputRight[] = {-0.25f, 0.5f};
    const float* inputs[] = {inputLeft, inputRight};
    std::array<std::array<float, 2>, 10> storage{};
    std::array<float*, 10> outputs{};
    for (std::size_t channel = 0; channel < outputs.size(); ++channel)
        outputs[channel] = storage[channel].data();

    AudioInputPair::StereoControls stereo;
    stereo.gain = 0.5f;
    stereo.auxSend = {{0.125f, 0.25f, 0.5f, 1.0f}};
    assert(bridge.enqueueAudioInputPairLinked(true));
    assert(bridge.enqueueAudioInputPairStereoControls(stereo));

    const AudioDuplexBuffer block{inputs, outputs.data(), 2, 10, 2};
    assert(backend.processBlock(block) == AudioDuplexStatus::Processed);
    assert(near(storage[0][0], 0.25f));
    assert(near(storage[1][0], -0.125f));
    for (std::size_t bus = 0; bus < AudioEngine::kAuxBusCount; ++bus) {
        const std::size_t leftChannel = 2 + bus * 2;
        const std::size_t rightChannel = leftChannel + 1;
        const float send = stereo.auxSend[bus];
        assert(near(storage[leftChannel][0], 0.25f * send));
        assert(near(storage[rightChannel][0], -0.125f * send));
        assert(near(storage[leftChannel][1], -0.125f * send));
        assert(near(storage[rightChannel][1], 0.25f * send));
    }

    // Master gain/mute affect the direct mix but not the pre-Master Aux taps.
    assert(bridge.enqueueMasterGain(0.5f));
    assert(backend.processBlock(block) == AudioDuplexStatus::Processed);
    assert(near(storage[0][0], 0.125f));
    assert(near(storage[1][0], -0.0625f));
    assert(near(storage[2][0], 0.25f * stereo.auxSend[0]));
    assert(near(storage[3][0], -0.125f * stereo.auxSend[0]));

    assert(bridge.enqueueMasterMute(true));
    assert(backend.processBlock(block) == AudioDuplexStatus::Processed);
    assert(near(storage[0][0], 0.0f) && near(storage[1][0], 0.0f));
    assert(near(storage[2][0], 0.25f * stereo.auxSend[0]));
    assert(near(storage[3][0], -0.125f * stereo.auxSend[0]));

    // Invalid odd output layouts silence the complete main pair but leave an
    // orphaned, unsupported third plane untouched.
    float malformedLeft[] = {3.0f, 3.0f};
    float malformedRight[] = {3.0f, 3.0f};
    float orphan[] = {7.0f, 7.0f};
    float* malformedOutputs[] = {malformedLeft, malformedRight, orphan};
    const AudioDuplexBuffer malformed{inputs, malformedOutputs, 2, 3, 2};
    host.processBlock(malformed);
    assert(near(malformedLeft[0], 0.0f));
    assert(near(malformedRight[0], 0.0f));
    assert(near(orphan[0], 7.0f));

    host.stop();
    host.shutdown();
    assert(host.setStereoInputPairEnabled(false));

    // The ordinary mono-track path also reaches the first logical Aux pair.
    assert(host.setInputRoute(0, 0));
    assert(host.setInputRoute(1, 1));
    format.outputChannels = 4; // Main plus Aux bus 0 only.
    assert(host.prepare(backend, format));
    AudioEngine::Track leftTrack;
    leftTrack.gain = 0.5f;
    leftTrack.pan = -1.0f;
    leftTrack.auxSend[0] = 0.5f;
    AudioEngine::Track rightTrack;
    rightTrack.gain = 0.25f;
    rightTrack.pan = 1.0f;
    rightTrack.auxSend[0] = 1.0f;
    assert(bridge.enqueueTrack(0, leftTrack));
    assert(bridge.enqueueTrack(1, rightTrack));
    assert(bridge.enqueueMasterGain(1.0f));
    assert(bridge.enqueueMasterMute(false));
    assert(host.start());

    float mainLeft[2] = {}, mainRight[2] = {};
    float auxLeft[2] = {}, auxRight[2] = {};
    float* partialOutputs[] = {mainLeft, mainRight, auxLeft, auxRight};
    const AudioDuplexBuffer partial{inputs, partialOutputs, 2, 4, 2};
    assert(backend.processBlock(partial) == AudioDuplexStatus::Processed);
    assert(near(mainLeft[0], 0.25f));
    assert(near(mainRight[0], -0.0625f));
    assert(near(auxLeft[0], 0.125f));
    assert(near(auxRight[0], -0.0625f));

    host.stop();
    host.shutdown();
    return 0;
}
