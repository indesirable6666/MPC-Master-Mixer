#include "../src/mixer/audio_meter_ui_consumer.hpp"

#include <cassert>
#include <cmath>
#include <limits>

namespace {
bool near(float a, float b) { return std::fabs(a - b) < 0.0001f; }
}

int main() {
    mpcmixer::AudioMeterSnapshotBridge bridge;
    mpcmixer::AudioMeterUiConsumer consumer(bridge, 0.0f);

    mpcmixer::AudioMeterSnapshot clipping;
    clipping.blockSequence = 1;
    for (std::size_t i = 0; i < mpcmixer::AudioMeterSnapshot::kChannelCount; ++i) {
        clipping.channels[i] = {mpcmixer::MeterModel::kClipThreshold, 0.42f};
    }
    assert(bridge.publish(clipping));
    assert(consumer.tick(0.0f));

    // All 16 tracks and Master latch independently at the clip threshold.
    for (std::size_t i = 0; i < mpcmixer::AudioMeterSnapshot::kChannelCount; ++i) {
        assert(consumer.meter(i).clipped());
        assert(near(consumer.meter(i).peak(), mpcmixer::MeterModel::kClipThreshold));
        assert(near(consumer.meter(i).rms(), 0.42f));
    }

    // Lower levels update RMS but do not clear any latched clip indication.
    mpcmixer::AudioMeterSnapshot quiet;
    quiet.blockSequence = 2;
    for (auto& channel : quiet.channels) channel = {0.2f, 0.1f};
    assert(bridge.publish(quiet));
    assert(consumer.tick(0.0f));
    for (std::size_t i = 0; i < mpcmixer::AudioMeterSnapshot::kChannelCount; ++i) {
        assert(consumer.meter(i).clipped());
        assert(near(consumer.meter(i).rms(), 0.1f));
    }

    // Resetting one channel clears only its clip latch, not its meter values.
    assert(consumer.resetClip(0));
    assert(!consumer.meter(0).clipped());
    assert(near(consumer.meter(0).peak(), mpcmixer::MeterModel::kClipThreshold));
    assert(near(consumer.meter(0).rms(), 0.1f));
    assert(consumer.meter(1).clipped());
    assert(consumer.meter(mpcmixer::AudioMeterSnapshot::kMasterIndex).clipped());
    assert(!consumer.resetClip(mpcmixer::AudioMeterSnapshot::kChannelCount));

    // Bulk reset clears all remaining latches without resetting peak or RMS.
    consumer.resetAllClips();
    for (std::size_t i = 0; i < mpcmixer::AudioMeterSnapshot::kChannelCount; ++i) {
        assert(!consumer.meter(i).clipped());
        assert(near(consumer.meter(i).peak(), mpcmixer::MeterModel::kClipThreshold));
        assert(near(consumer.meter(i).rms(), 0.1f));
    }

    // Invalid measurements are ignored and cannot re-latch or corrupt values.
    mpcmixer::AudioMeterSnapshot invalid;
    invalid.blockSequence = 3;
    invalid.channels[0] = {std::numeric_limits<float>::quiet_NaN(), 0.3f};
    invalid.channels[1] = {std::numeric_limits<float>::infinity(), 0.3f};
    invalid.channels[2] = {-0.1f, 0.3f};
    invalid.channels[3] = {0.3f, std::numeric_limits<float>::quiet_NaN()};
    assert(bridge.publish(invalid));
    assert(consumer.tick(0.0f));
    for (std::size_t i = 0; i < 4; ++i) {
        assert(!consumer.meter(i).clipped());
        assert(near(consumer.meter(i).peak(), mpcmixer::MeterModel::kClipThreshold));
        assert(near(consumer.meter(i).rms(), 0.1f));
    }
    return 0;
}
