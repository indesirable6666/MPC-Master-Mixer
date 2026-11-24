#pragma once

#include "native_ui_command_adapter.hpp"
#include "../../third_party/nlohmann/json.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace mpcmixer {

struct NativeUiProtocolV1Command {
    std::uint64_t epoch = 0;
    NativeUiCommandMessage command{};
};

enum class NativeUiProtocolV1DecodeStatus : std::uint8_t {
    Ok, Empty, TooLarge, InvalidJson, InvalidMessage
};

// Strict decoder for the v1 command envelope. Session/epoch freshness and
// command sequencing policy remain the coordinator/dispatcher responsibility.
class NativeUiProtocolV1Decoder {
public:
    static constexpr std::size_t kMaximumPayloadBytes = 1024;

    static NativeUiProtocolV1DecodeStatus decode(
        const std::string& input, NativeUiProtocolV1Command& output) {
        if (input.empty()) return NativeUiProtocolV1DecodeStatus::Empty;
        if (input.size() > kMaximumPayloadBytes)
            return NativeUiProtocolV1DecodeStatus::TooLarge;

        SaxHandler handler;
        const bool parsed = nlohmann::json::sax_parse(
            input, &handler, nlohmann::json::input_format_t::json, true, false);
        if (!parsed || !handler.complete())
            return NativeUiProtocolV1DecodeStatus::InvalidJson;

        NativeUiProtocolV1Command candidate;
        if (!handler.makeCommand(candidate))
            return NativeUiProtocolV1DecodeStatus::InvalidMessage;
        output = candidate;
        return NativeUiProtocolV1DecodeStatus::Ok;
    }

private:
    class SaxHandler : public nlohmann::json_sax<nlohmann::json> {
        enum class Field : std::uint8_t {
            None, Version, Kind, Epoch, Sequence, Type, Track, Index,
            Value, Flag, FrequencyHz, Q, GainDb, SampleRate
        };

    public:
        bool null() override { return false; }

        bool boolean(bool value) override {
            if (!beginValue() || field_ != Field::Flag) return false;
            command_.flag = value;
            command_.hasFlag = true;
            finishValue();
            return true;
        }

        bool number_integer(number_integer_t value) override {
            if (value >= 0)
                return number_unsigned(static_cast<number_unsigned_t>(value));
            if (!beginValue()) return false;
            if (field_ == Field::Value) {
                if (value < static_cast<number_integer_t>(
                        (std::numeric_limits<int>::min)()))
                    return false;
                command_.value = static_cast<int>(value);
                command_.hasValue = true;
            } else if (!isFloatField(field_) ||
                       !setFloat(static_cast<float>(value))) {
                return false;
            }
            finishValue();
            return true;
        }

        bool number_unsigned(number_unsigned_t value) override {
            if (!beginValue()) return false;
            switch (field_) {
            case Field::Version:
                if (value != 1) return false;
                versionSeen_ = true;
                break;
            case Field::Epoch:
                epoch_ = static_cast<std::uint64_t>(value);
                epochSeen_ = true;
                break;
            case Field::Sequence:
                if (value == 0) return false;
                command_.sequence = static_cast<std::uint64_t>(value);
                sequenceSeen_ = true;
                break;
            case Field::Track:
                if (value > static_cast<number_unsigned_t>(
                        (std::numeric_limits<std::size_t>::max)())) return false;
                command_.track = static_cast<std::size_t>(value);
                command_.hasTrack = true;
                break;
            case Field::Index:
                if (value > static_cast<number_unsigned_t>(
                        (std::numeric_limits<std::size_t>::max)())) return false;
                command_.index = static_cast<std::size_t>(value);
                command_.hasIndex = true;
                break;
            case Field::Value:
                if (value > static_cast<number_unsigned_t>(
                        (std::numeric_limits<int>::max)())) return false;
                command_.value = static_cast<int>(value);
                command_.hasValue = true;
                break;
            default:
                if (!isFloatField(field_) ||
                    !setFloat(static_cast<float>(value)))
                    return false;
                break;
            }
            finishValue();
            return true;
        }

        bool number_float(number_float_t value, const string_t&) override {
            if (!beginValue() || !isFloatField(field_) ||
                !std::isfinite(value) ||
                value > static_cast<number_float_t>(
                    (std::numeric_limits<float>::max)()) ||
                value < -static_cast<number_float_t>(
                    (std::numeric_limits<float>::max)()) ||
                !setFloat(static_cast<float>(value)))
                return false;
            finishValue();
            return true;
        }

        bool string(string_t& value) override {
            if (!beginValue()) return false;
            if (field_ == Field::Kind) {
                if (value != "command") return false;
                kindSeen_ = true;
            } else if (field_ == Field::Type) {
                command_.type = value;
            } else {
                return false;
            }
            finishValue();
            return true;
        }

        bool binary(binary_t&) override { return false; }

        bool start_object(std::size_t) override {
            if (rootStarted_ || rootEnded_ || depth_ != 0) return false;
            rootStarted_ = true;
            depth_ = 1;
            return true;
        }

        bool key(string_t& value) override {
            if (depth_ != 1 || rootEnded_ || field_ != Field::None) return false;
            Field next = Field::None;
            std::uint16_t bit = 0;
            if (value == "version") { next = Field::Version; bit = kVersionBit; }
            else if (value == "kind") { next = Field::Kind; bit = kKindBit; }
            else if (value == "epoch") { next = Field::Epoch; bit = kEpochBit; }
            else if (value == "sequence") { next = Field::Sequence; bit = kSequenceBit; }
            else if (value == "type") { next = Field::Type; bit = kTypeBit; }
            else if (value == "track") { next = Field::Track; bit = kTrackBit; }
            else if (value == "index") { next = Field::Index; bit = kIndexBit; }
            else if (value == "value") { next = Field::Value; bit = kValueBit; }
            else if (value == "flag") { next = Field::Flag; bit = kFlagBit; }
            else if (value == "frequencyHz") { next = Field::FrequencyHz; bit = kFrequencyBit; }
            else if (value == "q") { next = Field::Q; bit = kQBit; }
            else if (value == "gainDb") { next = Field::GainDb; bit = kGainBit; }
            else if (value == "sampleRate") { next = Field::SampleRate; bit = kSampleRateBit; }
            else return false;
            if ((seen_ & bit) != 0) return false;
            seen_ = static_cast<std::uint16_t>(seen_ | bit);
            field_ = next;
            return true;
        }

        bool end_object() override {
            if (depth_ != 1 || field_ != Field::None || rootEnded_) return false;
            depth_ = 0;
            rootEnded_ = true;
            return true;
        }

        bool start_array(std::size_t) override { return false; }
        bool end_array() override { return false; }

        bool parse_error(std::size_t, const string_t&,
            const nlohmann::detail::exception&) override { return false; }

        bool complete() const noexcept {
            return rootStarted_ && rootEnded_ && depth_ == 0 &&
                field_ == Field::None;
        }

        bool makeCommand(NativeUiProtocolV1Command& output) const {
            if (!complete() || !versionSeen_ || !kindSeen_ ||
                !epochSeen_ || !sequenceSeen_ || !typeSeen() || epoch_ == 0)
                return false;

            const std::uint16_t base = kVersionBit | kKindBit |
                kEpochBit | kSequenceBit | kTypeBit;
            const std::uint16_t trackValueFields =
                base | kTrackBit | kValueBit;
            const std::uint16_t trackFlagFields =
                base | kTrackBit | kFlagBit;
            const std::uint16_t masterValueFields = base | kValueBit;
            const std::uint16_t masterFlagFields = base | kFlagBit;
            const std::uint16_t pairValueFields = base | kValueBit;
            const std::uint16_t pairFlagFields = base | kFlagBit;
            const std::uint16_t indexedValueFields =
                base | kTrackBit | kIndexBit | kValueBit;
            const std::uint16_t eqFields = base | kTrackBit | kIndexBit |
                kFrequencyBit | kQBit | kGainBit;

            const std::string& type = command_.type;
            if (type == "SetTrackFader" || type == "SetTrackPan" ||
                type == "SetAudioInputChannelGain" ||
                type == "SetAudioInputChannelPan") {
                if (seen_ != trackValueFields ||
                    !command_.hasTrack || !command_.hasValue)
                    return false;
            } else if (type == "SetTrackMute" || type == "SetTrackSolo" ||
                       type == "SetAudioInputChannelMute" ||
                       type == "SetAudioInputChannelSolo") {
                if (seen_ != trackFlagFields ||
                    !command_.hasTrack || !command_.hasFlag)
                    return false;
            } else if (type == "SetMasterFader") {
                if (seen_ != masterValueFields || !command_.hasValue)
                    return false;
            } else if (type == "SetMasterMute") {
                if (seen_ != masterFlagFields || !command_.hasFlag)
                    return false;
            } else if (type == "SetAudioInputPairStereoGain" ||
                       type == "SetAudioInputPairBalance") {
                if (seen_ != pairValueFields || !command_.hasValue)
                    return false;
            } else if (type == "SetAudioInputPairLinked" ||
                       type == "SetAudioInputPairStereoMute") {
                if (seen_ != pairFlagFields || !command_.hasFlag)
                    return false;
            } else if (type == "SetTrackAuxSend" ||
                       type == "SetAudioInputChannelAuxSend") {
                if (seen_ != indexedValueFields || !command_.hasTrack ||
                    !command_.hasIndex || !command_.hasValue)
                    return false;
            } else if (type == "SetTrackEqBand") {
                const bool sampleRatePresent = (seen_ & kSampleRateBit) != 0;
                if ((seen_ != eqFields &&
                     seen_ != static_cast<std::uint16_t>(eqFields | kSampleRateBit)) ||
                    !command_.hasTrack || !command_.hasIndex ||
                    !command_.hasFrequencyHz || !command_.hasQ ||
                    !command_.hasGainDb ||
                    (sampleRatePresent && !command_.hasSampleRate))
                    return false;
            } else if (type == "SetAudioInputChannelEqBand") {
                if (seen_ != static_cast<std::uint16_t>(
                        eqFields | kSampleRateBit) ||
                    !command_.hasTrack || !command_.hasIndex ||
                    !command_.hasFrequencyHz || !command_.hasQ ||
                    !command_.hasGainDb || !command_.hasSampleRate)
                    return false;
            } else {
                return false;
            }

            output.epoch = epoch_;
            output.command = command_;
            return true;
        }

    private:
        static constexpr std::uint16_t kVersionBit = 1u << 0;
        static constexpr std::uint16_t kKindBit = 1u << 1;
        static constexpr std::uint16_t kEpochBit = 1u << 2;
        static constexpr std::uint16_t kSequenceBit = 1u << 3;
        static constexpr std::uint16_t kTypeBit = 1u << 4;
        static constexpr std::uint16_t kTrackBit = 1u << 5;
        static constexpr std::uint16_t kIndexBit = 1u << 6;
        static constexpr std::uint16_t kValueBit = 1u << 7;
        static constexpr std::uint16_t kFlagBit = 1u << 8;
        static constexpr std::uint16_t kFrequencyBit = 1u << 9;
        static constexpr std::uint16_t kQBit = 1u << 10;
        static constexpr std::uint16_t kGainBit = 1u << 11;
        static constexpr std::uint16_t kSampleRateBit = 1u << 12;

        bool beginValue() const noexcept {
            return rootStarted_ && !rootEnded_ && depth_ == 1 &&
                field_ != Field::None;
        }

        static bool isFloatField(Field field) noexcept {
            return field == Field::FrequencyHz || field == Field::Q ||
                field == Field::GainDb || field == Field::SampleRate;
        }

        bool setFloat(float value) noexcept {
            if (!std::isfinite(value)) return false;
            switch (field_) {
            case Field::FrequencyHz:
                command_.frequencyHz = value;
                command_.hasFrequencyHz = true;
                return true;
            case Field::Q:
                command_.q = value;
                command_.hasQ = true;
                return true;
            case Field::GainDb:
                command_.gainDb = value;
                command_.hasGainDb = true;
                return true;
            case Field::SampleRate:
                command_.sampleRate = value;
                command_.hasSampleRate = true;
                return true;
            default:
                return false;
            }
        }

        void finishValue() noexcept { field_ = Field::None; }
        bool typeSeen() const noexcept { return (seen_ & kTypeBit) != 0; }

        NativeUiCommandMessage command_{};
        Field field_ = Field::None;
        std::uint16_t seen_ = 0;
        std::uint64_t epoch_ = 0;
        std::size_t depth_ = 0;
        bool rootStarted_ = false;
        bool rootEnded_ = false;
        bool versionSeen_ = false;
        bool kindSeen_ = false;
        bool epochSeen_ = false;
        bool sequenceSeen_ = false;
    };
};

} // namespace mpcmixer
