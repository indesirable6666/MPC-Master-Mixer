#include "../src/mixer/audio_meter_ui_consumer.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>

namespace {
bool near(float a, float b, float tolerance = 0.0001f) {
    return std::fabs(a - b) <= tolerance;
}
}

int main() {
    mpcmixer::AudioMeterSnapshotBridge bridge;
    mpcmixer::AudioMeterUiConsumer consumer(bridge, 1.0f);

    // Fill the bounded snapshot queue with a monotonically increasing block
    // sequence. Overflow rejects the newest frame rather than overwriting
    // unread data, so the UI can only advance to the latest accepted frame.
    for (std::uint64_t sequence = 10; sequence < 18; ++sequence) {
        mpcmixer::AudioMeterSnapshot frame;
        frame.blockSequence = sequence;
        frame.channels[0] = {
            static_cast<float>(sequence) / 20.0f,
            static_cast<float>(sequence) / 40.0f
        };
        assert(bridge.publish(frame));
    }
    mpcmixer::AudioMeterSnapshot overflow;
    overflow.blockSequence = 18;
    overflow.channels[0] = {0.99f, 0.8f};
    assert(!bridge.publish(overflow));
    assert(bridge.pendingCountApprox() ==
           mpcmixer::AudioMeterSnapshotBridge::kQueueCapacity);

    assert(consumer.tick(0.0f));
    assert(consumer.lastSequence() == 17);
    assert(near(consumer.meter(0).peak(), 0.85f));
    assert(near(consumer.meter(0).rms(), 17.0f / 40.0f));
    assert(bridge.pendingCountApprox() == 0);

    // Sequence gaps are expected after dropped frames. The next newer frame
    // is accepted and replaces RMS while peak hold remains monotonic.
    mpcmixer::AudioMeterSnapshot recovered;
    recovered.blockSequence = 23;
    recovered.channels[0] = {0.3f, 0.2f};
    assert(bridge.publish(recovered));
    assert(consumer.tick(0.0f));
    assert(consumer.lastSequence() == 23);
    assert(near(consumer.meter(0).peak(), 0.85f));
    assert(near(consumer.meter(0).rms(), 0.2f));

    // Delayed frames cannot roll RMS or the source sequence backwards.
    mpcmixer::AudioMeterSnapshot delayed;
    delayed.blockSequence = 22;
    delayed.channels[0] = {1.0f, 1.0f};
    assert(bridge.publish(delayed));
    assert(!consumer.tick(0.0f));
    assert(consumer.lastSequence() == 23);
    assert(near(consumer.meter(0).peak(), 0.85f));
    assert(near(consumer.meter(0).rms(), 0.2f));

    // A later valid block recovers normally after the stale frame is drained.
    mpcmixer::AudioMeterSnapshot next;
    next.blockSequence = 24;
    next.channels[0] = {0.95f, 0.4f};
    assert(bridge.publish(next));
    assert(consumer.tick(0.0f));
    assert(consumer.lastSequence() == 24);
    assert(near(consumer.meter(0).peak(), 0.95f));
    assert(near(consumer.meter(0).rms(), 0.4f));

    return 0;
}
