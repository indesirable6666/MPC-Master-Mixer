#include "../src/mixer/meter_presentation_adapter.hpp"

#include <cassert>
#include <cmath>
#include <vector>

namespace {
class FakeSink final : public mpcmixer::MeterPresentationSink {
public:
    void present(const mpcmixer::MeterPresentationFrame& frame) override {
        frames.push_back(frame); // copy: models a UI-owned queued value
    }
    std::vector<mpcmixer::MeterPresentationFrame> frames;
};

bool near(float a, float b, float tolerance = 0.0001f) {
    return std::fabs(a - b) <= tolerance;
}
}

int main() {
    mpcmixer::AudioMeterSnapshotBridge bridge;
    mpcmixer::AudioMeterUiConsumer consumer(bridge, 2.0f);
    FakeSink sink;
    mpcmixer::MeterPresentationAdapter adapter(consumer, sink);

    // No data means no presentation callback.
    assert(!adapter.tick(1.0f));
    assert(sink.frames.empty());

    mpcmixer::AudioMeterSnapshot first;
    first.blockSequence = 41;
    first.channels[0] = {0.8f, 0.4f};
    first.channels[16] = {0.7f, 0.35f};
    assert(bridge.publish(first));

    assert(adapter.tick(0.0f));
    assert(sink.frames.size() == 1);
    assert(sink.frames.back().sequence == 1);
    assert(sink.frames.back().sourceBlockSequence == 41);
    assert(near(sink.frames.back().channels[0].peak, 0.8f));
    assert(near(sink.frames.back().channels[0].rms, 0.4f));
    assert(near(sink.frames.back().channels[16].peak, 0.7f));

    // Decay-only UI ticks emit a fresh presentation sequence while retaining
    // the same source audio block sequence. This is accepted by strict UI
    // presentation-sequence validation and avoids a second decay implementation.
    assert(adapter.tick(0.1f));
    assert(sink.frames.size() == 2);
    assert(sink.frames.back().sequence == 2);
    assert(sink.frames.back().sourceBlockSequence == 41);
    assert(near(sink.frames.back().channels[0].peak, 0.6f));
    assert(near(sink.frames.back().channels[0].rms, 0.4f));

    // A zero-duration tick with no new source frame emits nothing.
    assert(!adapter.tick(0.0f));
    assert(sink.frames.size() == 2);

    mpcmixer::AudioMeterSnapshot second;
    second.blockSequence = 42;
    second.channels[0] = {0.9f, 0.45f};
    assert(bridge.publish(second));
    assert(adapter.tick(-1.0f)); // New snapshots are emitted even with invalid dt.
    assert(sink.frames.size() == 3);
    assert(sink.frames.back().sequence == 3);
    assert(sink.frames.back().sourceBlockSequence == 42);
    assert(near(sink.frames.back().channels[0].peak, 0.9f));
    assert(adapter.presentationSequence() == 3);

    return 0;
}
