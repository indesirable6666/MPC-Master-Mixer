#include "../src/audio/audio_control_bridge.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

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

    // A drained queue remains reusable after many complete ring rotations.
    assert(bridge.enqueueMasterGain(0.73f));
    assert(bridge.pendingCountApprox() == 1);
    bridge.process(inputs, left, right, 1);
    assert(bridge.pendingCountApprox() == 0);
    assert(std::fabs(bridge.engineOnAudioThread().masterGain() - 0.73f) <
           0.00001f);

    return 0;
}
