#include "../src/audio/audio_control_bridge.hpp"
#include "../src/mixer/mixer_controller.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
bool near(float actual, float expected, float tolerance = 1.0e-5f) {
    return std::fabs(actual - expected) <= tolerance;
}
}

int main() {
    using Engine = mpcmixer::AudioEngine;
    using Bridge = mpcmixer::AudioEngineCommandBridge;

    Bridge bridge;
    mpcmixer::MixerController controller(bridge);

    // Four independent send levels on one track are published as track-state
    // snapshots. The audio thread sees them together at the next block edge.
    assert(controller.setTrackFader(0, 100));
    assert(controller.setTrackPan(0, 0));
    assert(controller.setTrackAuxSend(0, 0, 25));
    assert(controller.setTrackAuxSend(0, 1, 50));
    assert(controller.setTrackAuxSend(0, 2, 100));
    assert(controller.setTrackAuxSend(0, 3, 75));

    Engine::AuxReturn return0;
    return0.gain = 0.5f;
    return0.balance = 0.5f;
    Engine::AuxReturn return2;
    return2.gain = 0.25f;
    return2.balance = -0.5f;
    assert(controller.setAuxReturn(0, return0));
    assert(controller.setAuxReturn(2, return2));
    assert(bridge.pendingCountApprox() == 7);

    float input[] = {1.0f};
    const float* inputs[Engine::kTrackCount] = {};
    inputs[0] = input;
    std::array<float, Engine::kAuxBusCount> sendL{};
    std::array<float, Engine::kAuxBusCount> sendR{};
    std::array<float*, Engine::kAuxBusCount> sendLeft{};
    std::array<float*, Engine::kAuxBusCount> sendRight{};
    for (std::size_t bus = 0; bus < Engine::kAuxBusCount; ++bus) {
        sendLeft[bus] = &sendL[bus];
        sendRight[bus] = &sendR[bus];
    }

    float return0L[] = {0.1f};
    float return0R[] = {0.2f};
    float return2L[] = {0.4f};
    float return2R[] = {0.8f};
    const float* returnsL[Engine::kAuxBusCount] = {
        return0L, nullptr, return2L, nullptr
    };
    const float* returnsR[Engine::kAuxBusCount] = {
        return0R, nullptr, return2R, nullptr
    };
    float mainL[] = {};
    float mainR[] = {};
    bridge.processWithAuxAndReturns(inputs, mainL, mainR,
                                    sendLeft.data(), sendRight.data(),
                                    returnsL, returnsR, 1);

    const float centre = std::sqrt(0.5f);
    assert(bridge.pendingCountApprox() == 0);
    assert(near(sendL[0], 0.25f * centre));
    assert(near(sendL[1], 0.50f * centre));
    assert(near(sendL[2], 1.00f * centre));
    assert(near(sendL[3], 0.75f * centre));
    for (std::size_t bus = 0; bus < Engine::kAuxBusCount; ++bus)
        assert(near(sendL[bus], sendR[bus]));

    // Returns 0 and 2 are independently balanced and gained, summed with the
    // dry track, and remain isolated from the four send output buffers.
    const float expectedLeft =
        centre + (0.1f * 0.5f * 0.5f) + (0.4f * 0.25f);
    const float expectedRight =
        centre + (0.2f * 0.5f) + (0.8f * 0.25f * 0.5f);
    assert(near(mainL[0], expectedLeft));
    assert(near(mainR[0], expectedRight));

    // Update only buses 1 and 3 and mute return 0. Bus 0/2/3 send values and
    // return 2 must remain unchanged; state is applied at the next block.
    assert(controller.setTrackAuxSend(0, 1, 0));
    assert(controller.setTrackAuxSend(0, 3, 20));
    return0.mute = true;
    assert(controller.setAuxReturn(0, return0));
    assert(bridge.pendingCountApprox() == 3);

    bridge.processWithAuxAndReturns(inputs, mainL, mainR,
                                    sendLeft.data(), sendRight.data(),
                                    returnsL, returnsR, 1);
    assert(bridge.pendingCountApprox() == 0);
    assert(near(sendL[0], 0.25f * centre));
    assert(near(sendL[1], 0.0f));
    assert(near(sendL[2], 1.0f * centre));
    assert(near(sendL[3], 0.20f * centre));
    assert(near(mainL[0], centre + 0.4f * 0.25f));
    assert(near(mainR[0], centre + 0.8f * 0.25f * 0.5f));

    // An invalid return update is rejected before enqueueing and does not
    // alter the controller's desired state.
    Engine::AuxReturn invalid = return2;
    invalid.gain = Engine::kMaxFaderGain + 0.01f;
    const std::size_t pendingBeforeInvalid = bridge.pendingCountApprox();
    assert(!controller.setAuxReturn(2, invalid));
    assert(bridge.pendingCountApprox() == pendingBeforeInvalid);
    assert(near(controller.desiredAuxReturn(2).gain, return2.gain));

    // Queue saturation is reported without partially changing controller
    // state: a failed publish must leave the desired Aux return untouched.
    Bridge fullBridge;
    mpcmixer::MixerController fullController(fullBridge);
    for (std::size_t i = 0; i < Bridge::kQueueCapacity; ++i)
        assert(fullBridge.enqueueMasterMute((i % 2) != 0));
    Engine::AuxReturn proposed;
    proposed.gain = 0.25f;
    proposed.balance = -0.75f;
    assert(!fullController.setAuxReturn(1, proposed));
    assert(near(fullController.desiredAuxReturn(1).gain, 1.0f));
    assert(near(fullController.desiredAuxReturn(1).balance, 0.0f));
    assert(fullBridge.pendingCountApprox() == Bridge::kQueueCapacity);

    float silenceL[] = {0.0f};
    const float* silenceInputs[Engine::kTrackCount] = {};
    fullBridge.process(silenceInputs, mainL, mainR, 0);
    assert(fullBridge.pendingCountApprox() == 0);
    assert(fullController.setAuxReturn(1, proposed));
    assert(fullBridge.pendingCountApprox() == 1);
    fullBridge.processWithAuxAndReturns(silenceInputs, mainL, mainR,
                                        nullptr, nullptr, nullptr, nullptr, 1);
    assert(near(fullBridge.engineOnAudioThread().auxReturn(1).gain, 0.25f));
    assert(near(fullBridge.engineOnAudioThread().auxReturn(1).balance, -0.75f));

    return 0;
}
