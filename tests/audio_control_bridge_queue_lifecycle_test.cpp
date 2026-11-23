#include "../src/audio/audio_control_bridge.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>

int main() {
    using Bridge = mpcmixer::AudioEngineCommandBridge;
    using Engine = mpcmixer::AudioEngine;

    // Ticket arithmetic must skip the reserved zero exactly once at wrap.
    static_assert(mpcmixer::detail::nextAudioCommandTicket(0u) == 1u,
                  "first command ticket must be one");
    static_assert(mpcmixer::detail::nextAudioCommandTicket(UINT32_MAX - 1u) ==
                      UINT32_MAX,
                  "ticket before wrap must be UINT32_MAX");
    static_assert(mpcmixer::detail::nextAudioCommandTicket(UINT32_MAX) == 1u,
                  "wrapped ticket must skip zero");
    static_assert(mpcmixer::detail::nextAudioCommandTicket(1u) == 2u,
                  "ticket after wrap must advance normally");

    Bridge bridge;
    const float* inputs[Engine::kTrackCount]{};
    float left[1]{};
    float right[1]{};

    // Reuse every ring slot repeatedly. Each batch fills the queue exactly,
    // rejects one extra command, then drains at a simulated block boundary.
    constexpr std::size_t kBatches = 256;
    for (std::size_t batch = 0; batch < kBatches; ++batch) {
        float expectedGain = 1.0f;
        for (std::size_t item = 0; item < Bridge::kQueueCapacity; ++item) {
            expectedGain = 0.25f +
                static_cast<float>((batch + item) % 100) / 100.0f;
            assert(bridge.enqueueMasterGain(expectedGain));
        }

        assert(bridge.pendingCountApprox() == Bridge::kQueueCapacity);
        assert(!bridge.enqueueMasterGain(0.1f));
        assert(bridge.pendingCountApprox() == Bridge::kQueueCapacity);

        bridge.process(inputs, left, right, 1);
        assert(bridge.pendingCountApprox() == 0);
        assert(std::fabs(bridge.engineOnAudioThread().masterGain() -
                         expectedGain) < 0.00001f);
    }

    // Aux Return validation rejects non-finite and out-of-range controls
    // without publishing a command to the real-time queue.
    Engine::AuxReturn auxReturn;
    auxReturn.gain = std::numeric_limits<float>::quiet_NaN();
    assert(!bridge.enqueueAuxReturn(0, auxReturn));
    auxReturn.gain = std::numeric_limits<float>::infinity();
    assert(!bridge.enqueueAuxReturn(0, auxReturn));
    auxReturn.gain = -0.01f;
    assert(!bridge.enqueueAuxReturn(0, auxReturn));
    auxReturn.gain = Engine::kMaxFaderGain + 0.01f;
    assert(!bridge.enqueueAuxReturn(0, auxReturn));
    auxReturn.gain = 1.0f;
    auxReturn.balance = std::numeric_limits<float>::quiet_NaN();
    assert(!bridge.enqueueAuxReturn(0, auxReturn));
    auxReturn.balance = std::numeric_limits<float>::infinity();
    assert(!bridge.enqueueAuxReturn(0, auxReturn));
    auxReturn.balance = -1.01f;
    assert(!bridge.enqueueAuxReturn(0, auxReturn));
    auxReturn.balance = 1.01f;
    assert(!bridge.enqueueAuxReturn(0, auxReturn));
    auxReturn.balance = 0.0f;
    assert(!bridge.enqueueAuxReturn(Engine::kAuxBusCount, auxReturn));
    assert(bridge.pendingCountApprox() == 0);

    // Aux Return commands obey the same bounded-queue backpressure: a full
    // queue rejects the command and leaves the current engine state unchanged.
    const auto originalReturn = bridge.engineOnAudioThread().auxReturn(0);
    for (std::size_t item = 0; item < Bridge::kQueueCapacity; ++item)
        assert(bridge.enqueueMasterMute((item & 1u) != 0));
    auxReturn.gain = 0.625f;
    auxReturn.balance = -0.25f;
    auxReturn.mute = true;
    assert(!bridge.enqueueAuxReturn(0, auxReturn));
    assert(bridge.pendingCountApprox() == Bridge::kQueueCapacity);
    assert(std::fabs(bridge.engineOnAudioThread().auxReturn(0).gain -
                     originalReturn.gain) < 0.00001f);
    assert(bridge.engineOnAudioThread().auxReturn(0).mute == originalReturn.mute);

    // The accepted update is invisible until the next process call drains the
    // queue, then all fields become visible together at that block boundary.
    bridge.process(inputs, left, right, 1);
    assert(bridge.pendingCountApprox() == 0);
    assert(bridge.enqueueAuxReturn(0, auxReturn));
    assert(std::fabs(bridge.engineOnAudioThread().auxReturn(0).gain -
                     originalReturn.gain) < 0.00001f);
    bridge.process(inputs, left, right, 1);
    const auto& appliedReturn = bridge.engineOnAudioThread().auxReturn(0);
    assert(std::fabs(appliedReturn.gain - 0.625f) < 0.00001f);
    assert(std::fabs(appliedReturn.balance + 0.25f) < 0.00001f);
    assert(appliedReturn.mute);

    // A drained queue remains reusable after many complete ring rotations.
    assert(bridge.enqueueMasterGain(0.73f));
    assert(bridge.pendingCountApprox() == 1);
    bridge.process(inputs, left, right, 1);
    assert(bridge.pendingCountApprox() == 0);
    assert(std::fabs(bridge.engineOnAudioThread().masterGain() - 0.73f) <
           0.00001f);

    return 0;
}
