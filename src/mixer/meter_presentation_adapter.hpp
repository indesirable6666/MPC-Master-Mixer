#pragma once

#include "audio_meter_ui_consumer.hpp"

#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mpcmixer {

// Owned value frame for a presentation runtime. The frame contains no pointers
// into the audio snapshot queue and is safe to copy into a UI-owned task.
struct MeterPresentationFrame {
    static constexpr std::size_t kChannelCount =
        AudioMeterSnapshot::kChannelCount;

    // Presentation sequence advances on every emitted UI frame, including
    // peak-decay-only frames. sourceBlockSequence identifies the latest audio
    // snapshot incorporated into this presentation state.
    std::uint64_t sequence = 0;
    std::uint64_t sourceBlockSequence = 0;
    std::array<AudioBlockMeterReading, kChannelCount> channels{};
};

// Implementations must copy the frame before returning if they enqueue work.
// Called only by MeterPresentationAdapter::tick on its owning UI/control thread.
class MeterPresentationSink {
public:
    virtual ~MeterPresentationSink() = default;
    virtual void present(const MeterPresentationFrame& frame) = 0;
};

// Framework-neutral adapter. It owns no thread/timer: the host calls tick()
// from its bounded UI cadence. It never runs on or calls back into audio.
class MeterPresentationAdapter {
public:
    explicit MeterPresentationAdapter(AudioMeterUiConsumer& consumer,
                                      MeterPresentationSink& sink) noexcept
        : consumer_(consumer), sink_(sink) {}

    bool tick(float elapsedSeconds) {
        const bool received = consumer_.tick(elapsedSeconds);
        if (!consumer_.hasSnapshot()) return false;

        // Publish on new audio data and on decay-only ticks so the UI can
        // animate peak fall without duplicating MeterModel's decay logic.
        if (!received && (!std::isfinite(elapsedSeconds) ||
                         elapsedSeconds <= 0.0f)) {
            return false;
        }

        MeterPresentationFrame frame;
        frame.sequence = ++presentationSequence_;
        frame.sourceBlockSequence = consumer_.lastSequence();
        for (std::size_t i = 0; i < MeterPresentationFrame::kChannelCount; ++i) {
            frame.channels[i].peak = consumer_.meter(i).peak();
            frame.channels[i].rms = consumer_.meter(i).rms();
        }
        sink_.present(frame);
        return true;
    }

    std::uint64_t presentationSequence() const noexcept {
        return presentationSequence_;
    }

private:
    AudioMeterUiConsumer& consumer_;
    MeterPresentationSink& sink_;
    std::uint64_t presentationSequence_ = 0;
};

} // namespace mpcmixer
