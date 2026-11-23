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
    format.inputChannels = 3; // Source pair plus an incomplete Aux Return.
    assert(!host.prepare(backend, format));
    format.inputChannels = AudioDuplexFormat::kMaxInputChannels + 1;
    assert(!format.valid());
    format.inputChannels = 2;
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

    // Aux Return 1 follows the source pair and is mixed through its own
    // gain, balance and mute controls into the Master output.
    host.stop();
    host.shutdown();
    assert(host.setInputRoute(0, AudioDuplexEngineHost::kUnmappedTrack));
    assert(host.setInputRoute(1, AudioDuplexEngineHost::kUnmappedTrack));

    AudioDuplexFormat returnFormat;
    returnFormat.inputChannels = 4; // Source L/R + Aux Return 1 L/R.
    returnFormat.outputChannels = 2;
    returnFormat.maxFramesPerBlock = 4;
    assert(returnFormat.valid());
    assert(host.prepare(backend, returnFormat));
    assert(host.start());

    const float returnInputLeft[] = {0.4f, -0.2f};
    const float returnInputRight[] = {-0.6f, 0.8f};
    const float* returnInputs[] = {
        inputLeft, inputRight, returnInputLeft, returnInputRight
    };
    float returnMainLeft[2] = {}, returnMainRight[2] = {};
    float* returnOutputs[] = {returnMainLeft, returnMainRight};
    const AudioDuplexBuffer returnBlock{returnInputs, returnOutputs, 4, 2, 2};

    AudioEngine::AuxReturn returnState;
    returnState.gain = 0.5f;
    returnState.balance = 0.5f;
    assert(bridge.enqueueAuxReturn(0, returnState));
    assert(backend.processBlock(returnBlock) == AudioDuplexStatus::Processed);
    assert(near(returnMainLeft[0], 0.1f));
    assert(near(returnMainRight[0], -0.3f));
    assert(near(returnMainLeft[1], -0.05f));
    assert(near(returnMainRight[1], 0.4f));

    returnState.mute = true;
    assert(bridge.enqueueAuxReturn(0, returnState));
    assert(backend.processBlock(returnBlock) == AudioDuplexStatus::Processed);
    assert(near(returnMainLeft[0], 0.0f));
    assert(near(returnMainRight[0], 0.0f));

    // A missing plane in a declared return pair rejects the block and clears
    // Main instead of processing only one side of the return.
    const float* malformedReturnInputs[] = {
        inputLeft, inputRight, returnInputLeft, nullptr
    };
    returnMainLeft[0] = returnMainRight[0] = 9.0f;
    const AudioDuplexBuffer malformedReturn{
        malformedReturnInputs, returnOutputs, 4, 2, 2
    };
    host.processBlock(malformedReturn);
    assert(near(returnMainLeft[0], 0.0f));
    assert(near(returnMainRight[0], 0.0f));

    host.stop();
    host.shutdown();

    // The ten-channel layout carries all four stereo Aux Returns alongside
    // the dedicated stereo Audio In source pair.
    assert(host.setStereoInputPairEnabled(true));
    returnFormat.inputChannels = 10;
    assert(host.prepare(backend, returnFormat));
    assert(host.start());

    const float return2Left[] = {0.2f, -0.4f};
    const float return2Right[] = {0.4f, -0.2f};
    const float return3Left[] = {-0.1f, 0.3f};
    const float return3Right[] = {0.2f, -0.1f};
    const float return4Left[] = {0.1f, -0.2f};
    const float return4Right[] = {-0.2f, 0.2f};
    const float* allReturnInputs[] = {
        inputLeft, inputRight,
        returnInputLeft, returnInputRight,
        return2Left, return2Right,
        return3Left, return3Right,
        return4Left, return4Right
    };
    const AudioDuplexBuffer allReturnBlock{
        allReturnInputs, returnOutputs, 10, 2, 2
    };

    returnState.mute = false;
    assert(bridge.enqueueAuxReturn(0, returnState));
    AudioEngine::AuxReturn secondReturn;
    secondReturn.gain = 0.25f;
    secondReturn.balance = -0.5f;
    assert(bridge.enqueueAuxReturn(1, secondReturn));
    AudioEngine::AuxReturn thirdReturn;
    thirdReturn.gain = 1.0f;
    assert(bridge.enqueueAuxReturn(2, thirdReturn));
    AudioEngine::AuxReturn fourthReturn;
    fourthReturn.gain = 0.5f;
    fourthReturn.balance = 0.25f;
    assert(bridge.enqueueAuxReturn(3, fourthReturn));

    assert(bridge.enqueueAudioInputPairLinked(true));
    AudioInputPair::StereoControls pairState;
    pairState.gain = 0.5f;
    assert(bridge.enqueueAudioInputPairStereoControls(pairState));
    returnMainLeft[0] = returnMainRight[0] = 0.0f;
    assert(backend.processBlock(allReturnBlock) == AudioDuplexStatus::Processed);
    assert(near(returnMainLeft[0], 0.3375f));
    assert(near(returnMainRight[0], -0.275f));

    host.stop();
    host.shutdown();
    return 0;
}
