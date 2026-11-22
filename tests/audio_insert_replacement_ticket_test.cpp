#include "../src/audio/audio_control_bridge.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace {
bool near(float actual, float expected) {
    return std::fabs(actual - expected) < 1.0e-5f;
}

float processOne(mpcmixer::AudioEngineCommandBridge& bridge) {
    float input[] = {0.5f};
    const float* inputs[mpcmixer::AudioEngine::kTrackCount] = {};
    inputs[0] = input;
    float left[] = {0.0f};
    float right[] = {0.0f};
    bridge.process(inputs, left, right, 1);
    return left[0] * 1.41421356237f;
}
} // namespace

int main() {
    using Bridge = mpcmixer::AudioEngineCommandBridge;

    Bridge bridge;
    mpcmixer::GainInsert oldEffect;
    mpcmixer::GainInsert replacement;
    assert(oldEffect.setGain(0.5f));
    assert(replacement.setGain(0.25f));
    oldEffect.setEnabled(true);
    replacement.setEnabled(true);

    std::uint32_t initialTicket = 0;
    assert(bridge.enqueueTrackInsert(0, 0, &oldEffect, initialTicket));
    assert(!bridge.hasApplied(initialTicket));
    assert(near(processOne(bridge), 0.25f));
    assert(bridge.hasApplied(initialTicket));

    // The previous object must remain alive until this acknowledgement.
    std::uint32_t replacementTicket = 0;
    assert(bridge.enqueueTrackInsert(0, 0, &replacement, replacementTicket));
    assert(!bridge.hasApplied(replacementTicket));
    assert(!bridge.hasApplied(replacementTicket + 1));
    assert(near(processOne(bridge), 0.125f));
    assert(bridge.hasApplied(replacementTicket));

    // Removing an insert is also a replacement and must be acknowledged.
    std::uint32_t removalTicket = 0;
    assert(bridge.enqueueTrackInsert(0, 0, nullptr, removalTicket));
    assert(!bridge.hasApplied(removalTicket));
    assert(near(processOne(bridge), 0.5f));
    assert(bridge.hasApplied(removalTicket));

    // Queue saturation must not issue a ticket for a command that was not queued.
    for (std::size_t i = 0; i < Bridge::kQueueCapacity; ++i)
        assert(bridge.enqueueMasterMute((i % 2) != 0));
    std::uint32_t untouchedTicket = 0x12345678u;
    assert(!bridge.enqueueTrackInsert(0, 0, &oldEffect, untouchedTicket));
    assert(untouchedTicket == 0x12345678u);

    return 0;
}
