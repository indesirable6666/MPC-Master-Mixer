#pragma once

#include "native_ui_command_adapter.hpp"
#include "../../third_party/nlohmann/json.hpp"

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
            None, Version, Kind, Epoch, Sequence, Type, Track, Value, Flag
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
            if (value < 0) return false;
            return number_unsigned(static_cast<number_unsigned_t>(value));
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
            case Field::Value:
                if (value > static_cast<number_unsigned_t>(
                        (std::numeric_limits<int>::max)())) return false;
                command_.value = static_cast<int>(value);
                command_.hasValue = true;
                break;
            default: return false;
            }
            finishValue();
            return true;
        }
        bool number_float(number_float_t, const string_t&) override { return false; }
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
            if (value == "version") { next = Field::Version; bit = 1u << 0; }
            else if (value == "kind") { next = Field::Kind; bit = 1u << 1; }
            else if (value == "epoch") { next = Field::Epoch; bit = 1u << 2; }
            else if (value == "sequence") { next = Field::Sequence; bit = 1u << 3; }
            else if (value == "type") { next = Field::Type; bit = 1u << 4; }
            else if (value == "track") { next = Field::Track; bit = 1u << 5; }
            else if (value == "value") { next = Field::Value; bit = 1u << 6; }
            else if (value == "flag") { next = Field::Flag; bit = 1u << 7; }
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
                !epochSeen_ || !sequenceSeen_ || !typeSeen())
                return false;
            if (epoch_ == 0) return false;

            const bool trackValue = command_.type == "SetTrackFader" ||
                command_.type == "SetTrackPan";
            const bool trackFlag = command_.type == "SetTrackMute" ||
                command_.type == "SetTrackSolo";
            const bool masterValue = command_.type == "SetMasterFader";
            const bool masterFlag = command_.type == "SetMasterMute";
            const std::uint16_t base = 0x1fu; // version, kind, epoch, sequence, type
            if (trackValue || trackFlag) {
                if (!command_.hasTrack || ((seen_ & (1u << 5)) == 0)) return false;
                if (trackValue) {
                    if (!command_.hasValue || command_.hasFlag ||
                        seen_ != static_cast<std::uint16_t>(base | (1u << 5) | (1u << 6)))
                        return false;
                } else if (!command_.hasFlag || command_.hasValue ||
                    seen_ != static_cast<std::uint16_t>(base | (1u << 5) | (1u << 7))) {
                    return false;
                }
            } else if (masterValue) {
                if (!command_.hasValue || command_.hasTrack || command_.hasFlag ||
                    seen_ != static_cast<std::uint16_t>(base | (1u << 6))) return false;
            } else if (masterFlag) {
                if (!command_.hasFlag || command_.hasTrack || command_.hasValue ||
                    seen_ != static_cast<std::uint16_t>(base | (1u << 7))) return false;
            } else return false;

            output.epoch = epoch_;
            output.command = command_;
            return true;
        }

    private:
        bool beginValue() const noexcept {
            return rootStarted_ && !rootEnded_ && depth_ == 1 &&
                field_ != Field::None;
        }
        void finishValue() noexcept { field_ = Field::None; }
        bool typeSeen() const noexcept { return (seen_ & (1u << 4)) != 0; }

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
