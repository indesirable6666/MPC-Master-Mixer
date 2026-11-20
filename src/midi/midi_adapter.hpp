#pragma once

#include <cstdint>
#include <cstddef>
#include <array>
#include <cmath>
#include <vector>

namespace mpcmixer {

struct MidiMessage {
    std::uint8_t status;
    std::uint8_t data1;
    std::uint8_t data2;
};

class MidiTransport {
public:
    virtual ~MidiTransport() = default;
    virtual bool send(const MidiMessage& message) = 0;
};

// Deliberately inert transport for UI development and automated tests.
// It records messages in memory and never opens ALSA or a device.
class SimulatedMidiTransport final : public MidiTransport {
public:
    bool send(const MidiMessage& message) override {
        messages_.push_back(message);
        return true;
    }

    const std::vector<MidiMessage>& messages() const { return messages_; }
    void clear() { messages_.clear(); }

private:
    std::vector<MidiMessage> messages_;
};

struct CcMapping {
    std::uint8_t channel;    // MIDI channel: 0..15
    std::uint8_t controller; // CC number: 0..127
    bool explicitly_verified;
};

class VolumeMidiAdapter {
public:
    explicit VolumeMidiAdapter(MidiTransport& transport)
        : transport_(transport), mapping_{0, 0, false} {}

    // No mapping is active until the caller supplies one and marks it verified.
    bool configure(const CcMapping& mapping) {
        if (!mapping.explicitly_verified ||
            mapping.channel > 15 || mapping.controller > 127) {
            mapping_ = CcMapping{0, 0, false};
            return false;
        }
        mapping_ = mapping;
        return true;
    }

    void clearMapping() { mapping_ = CcMapping{0, 0, false}; }
    bool hasVerifiedMapping() const { return mapping_.explicitly_verified; }

    // Normalized UI volume [0,1] becomes a 7-bit CC value.
    // This sends only through the injected transport; this project currently
    // provides a simulated transport, not a live ALSA implementation.
    bool setVolume(float normalized) {
        if (!hasVerifiedMapping() || !std::isfinite(normalized) ||
            normalized < 0.0f || normalized > 1.0f) {
            return false;
        }
        const auto value = static_cast<std::uint8_t>(normalized * 127.0f + 0.5f);
        const MidiMessage message{
            static_cast<std::uint8_t>(0xB0u | mapping_.channel),
            mapping_.controller,
            value
        };
        return transport_.send(message);
    }

private:
    MidiTransport& transport_;
    CcMapping mapping_;
};

};

// Independent, fail-closed CC mapping for each of the 16 mixer tracks.
// Track indices are zero-based (0..15); no MIDI assignment is assumed.
class TrackVolumeMidiAdapter {
public:
    static constexpr std::size_t kTrackCount = 16;

    explicit TrackVolumeMidiAdapter(MidiTransport& transport)
        : transport_(transport), mappings_{} {}

    bool configureTrack(std::size_t trackIndex, const CcMapping& mapping) {
        if (trackIndex >= kTrackCount) {
            return false;
        }
        if (!mapping.explicitly_verified ||
            mapping.channel > 15 || mapping.controller > 127) {
            mappings_[trackIndex] = CcMapping{0, 0, false};
            return false;
        }
        mappings_[trackIndex] = mapping;
        return true;
    }

    void clearTrack(std::size_t trackIndex) {
        if (trackIndex < kTrackCount) {
            mappings_[trackIndex] = CcMapping{0, 0, false};
        }
    }

    void clearAll() {
        for (auto& mapping : mappings_) {
            mapping = CcMapping{0, 0, false};
        }
    }

    bool hasVerifiedMapping(std::size_t trackIndex) const {
        return trackIndex < kTrackCount &&
               mappings_[trackIndex].explicitly_verified;
    }

    bool setTrackVolume(std::size_t trackIndex, float normalized) {
        if (!hasVerifiedMapping(trackIndex) || !std::isfinite(normalized) ||
            normalized < 0.0f || normalized > 1.0f) {
            return false;
        }
        const CcMapping& mapping = mappings_[trackIndex];
        const auto value = static_cast<std::uint8_t>(normalized * 127.0f + 0.5f);
        const MidiMessage message{
            static_cast<std::uint8_t>(0xB0u | mapping.channel),
            mapping.controller,
            value
        };
        return transport_.send(message);
    }

private:
    MidiTransport& transport_;
    std::array<CcMapping, kTrackCount> mappings_;
};

} // namespace mpcmixer
