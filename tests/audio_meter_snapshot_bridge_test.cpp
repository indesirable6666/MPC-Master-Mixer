#include "../src/audio/audio_meter_snapshot_bridge.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>

int main() {
    mpcmixer::AudioMeterSnapshotBridge bridge;
    mpcmixer::AudioMeterSnapshot output;

    assert(!bridge.consume(output));
    assert(bridge.pendingCountApprox() == 0);

    mpcmixer::AudioMeterSnapshot first;
    first.blockSequence = 41;
    first.channels[0].peak = 0.75f;
    first.channels[0].rms = 0.25f;
    first.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak = 0.9f;
    first.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].rms = 0.5f;
    assert(bridge.publish(first));
    assert(bridge.pendingCountApprox() == 1);

    assert(bridge.consume(output));
    assert(output.blockSequence == 41);
    assert(std::fabs(output.channels[0].peak - 0.75f) < 0.00001f);
    assert(std::fabs(output.channels[0].rms - 0.25f) < 0.00001f);
    assert(std::fabs(output.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex].peak -
                     0.9f) < 0.00001f);
    assert(!bridge.consume(output));

    // A full queue rejects the newest snapshot without overwriting any
    // unread frame. The consumer must receive all accepted frames in order.
    for (std::uint64_t i = 0; i < mpcmixer::AudioMeterSnapshotBridge::kQueueCapacity; ++i) {
        mpcmixer::AudioMeterSnapshot frame;
        frame.blockSequence = 100 + i;
        frame.channels[3].peak = static_cast<float>(i) / 10.0f;
        assert(bridge.publish(frame));
    }
    mpcmixer::AudioMeterSnapshot rejected;
    rejected.blockSequence = 999;
    assert(!bridge.publish(rejected));
    assert(bridge.pendingCountApprox() ==
           mpcmixer::AudioMeterSnapshotBridge::kQueueCapacity);

    for (std::uint64_t i = 0; i < mpcmixer::AudioMeterSnapshotBridge::kQueueCapacity; ++i) {
        assert(bridge.consume(output));
        assert(output.blockSequence == 100 + i);
        assert(std::fabs(output.channels[3].peak -
                         static_cast<float>(i) / 10.0f) < 0.00001f);
    }
    assert(!bridge.consume(output));

    // Repeated publish/consume cycles exercise ring-index wraparound.
    for (std::uint64_t i = 0; i < 1000; ++i) {
        mpcmixer::AudioMeterSnapshot frame;
        frame.blockSequence = 1000 + i;
        frame.channels[7].rms = 0.125f;
        assert(bridge.publish(frame));
        assert(bridge.consume(output));
        assert(output.blockSequence == frame.blockSequence);
        assert(std::fabs(output.channels[7].rms - 0.125f) < 0.00001f);
    }
    assert(bridge.pendingCountApprox() == 0);
    return 0;
}
