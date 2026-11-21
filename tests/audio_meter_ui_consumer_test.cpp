#include "../src/mixer/audio_meter_ui_consumer.hpp"

#include <cassert>
#include <cmath>

namespace {
bool near(float a, float b, float tolerance = 0.0001f) {
    return std::fabs(a - b) <= tolerance;
}
}

int main() {
    mpcmixer::AudioMeterSnapshotBridge bridge;
    mpcmixer::AudioMeterUiConsumer consumer(bridge, 2.0f);
    assert(!consumer.hasSnapshot());
    assert(!consumer.tick(0.016f));

    mpcmixer::AudioMeterSnapshot first;
    first.blockSequence = 1;
    first.channels[0] = {0.8f, 0.4f};
    first.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex] = {0.6f, 0.3f};
    assert(bridge.publish(first));

    mpcmixer::AudioMeterSnapshot second;
    second.blockSequence = 2;
    second.channels[0] = {0.5f, 0.25f};
    second.channels[1] = {0.2f, 0.1f};
    second.channels[mpcmixer::AudioMeterSnapshot::kMasterIndex] = {0.7f, 0.35f};
    assert(bridge.publish(second));

    // Drain both queued frames: peak hold sees both; RMS ends on the newest frame.
    assert(consumer.tick(0.0f));
    assert(consumer.hasSnapshot());
    assert(consumer.lastSequence() == 2);
    assert(near(consumer.meter(0).peak(), 0.8f)); // peak hold across frames
    assert(near(consumer.meter(0).rms(), 0.25f)); // RMS from newest frame
    assert(near(consumer.meter(1).peak(), 0.2f));
    assert(near(consumer.meter(mpcmixer::AudioMeterSnapshot::kMasterIndex).peak(),
                0.7f));

    // Time-based decay applies on a UI tick with no new audio snapshot.
    assert(!consumer.tick(0.1f));
    assert(near(consumer.meter(0).peak(), 0.6f));
    assert(near(consumer.meter(0).rms(), 0.25f));

    // A newer peak refreshes the hold; invalid elapsed time is ignored.
    mpcmixer::AudioMeterSnapshot third;
    third.blockSequence = 3;
    third.channels[0] = {0.9f, 0.45f};
    assert(bridge.publish(third));
    assert(consumer.tick(-1.0f));
    assert(near(consumer.meter(0).peak(), 0.9f));
    assert(consumer.lastSequence() == 3);

    // A queued stale sequence cannot roll the UI state backwards.
    mpcmixer::AudioMeterSnapshot stale;
    stale.blockSequence = 2;
    stale.channels[0] = {0.1f, 0.05f};
    assert(bridge.publish(stale));
    assert(!consumer.tick(0.0f));
    assert(near(consumer.meter(0).peak(), 0.9f));
    assert(near(consumer.meter(0).rms(), 0.45f));

    return 0;
}
