#pragma once

#include "native_ui_command_adapter.hpp"
#include "../../third_party/nlohmann/json.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>
#include <string>

namespace mpcmixer {

enum class NativeUiJsonDecodeStatus : std::uint8_t {
    Ok,
    Empty,
    TooLarge,
    InvalidJson,
    InvalidMessage
};

class NativeUiJsonDecoder {
public:
    static constexpr std::size_t kMaximumFrameBytes = 1024;

    static NativeUiJsonDecodeStatus decode(
        const std::string& input,
        NativeUiCommandMessage& output) {
        if (input.empty())
            return NativeUiJsonDecodeStatus::Empty;
        if (input.size() > kMaximumFrameBytes)
            return NativeUiJsonDecodeStatus::TooLarge;

        SaxHandler handler;
        const bool parsed = nlohmann::json::sax_parse(
            input, &handler, nlohmann::json::input_format_t::json, true, false);
        if (!parsed || !handler.complete())
            return NativeUiJsonDecodeStatus::InvalidJson;

        NativeUiCommandMessage candidate;
        if (!handler.makeMessage(candidate))
            return NativeUiJsonDecodeStatus::InvalidMessage;

        output = candidate;
        return NativeUiJsonDecodeStatus::Ok;
    }

private:
    class SaxHandler : public nlohmann::json_sax<nlohmann::json> {
    public:
        bool null() override { return false; }

        bool boolean(bool value) override {
            if (!beginValue() || field_ != Field::Flag)
                return false;
            message_.flag = value;
            message_.hasFlag = true;
            finishValue();
            return true;
        }

        bool number_integer(number_integer_t value) override {
            if (!beginValue())
                return false;

            switch (field_) {
            case Field::Sequence:
                if (value <= 0)
                    return false;
                message_.sequence = static_cast<std::uint64_t>(value);
                break;
            case Field::Track:
                if (value < 0 ||
                    static_cast<std::uint64_t>(value) >
                        static_cast<std::uint64_t>(
                            (std::numeric_limits<std::size_t>::max)()))
                    return false;
                message_.track = static_cast<std::size_t>(value);
                message_.hasTrack = true;
                break;
            case Field::Value:
                if (value < (std::numeric_limits<int>::min)() ||
                    value > (std::numeric_limits<int>::max)())
                    return false;
                message_.value = static_cast<int>(value);
                message_.hasValue = true;
                break;
            default:
                return false;
            }
            finishValue();
            return true;
        }

        bool number_unsigned(number_unsigned_t value) override {
            if (!beginValue())
                return false;

            switch (field_) {
            case Field::Sequence:
                if (value == 0)
                    return false;
                message_.sequence = static_cast<std::uint64_t>(value);
                break;
            case Field::Track:
                if (value > static_cast<number_unsigned_t>(
                        (std::numeric_limits<std::size_t>::max)()))
                    return false;
                message_.track = static_cast<std::size_t>(value);
                message_.hasTrack = true;
                break;
            case Field::Value:
                if (value > static_cast<number_unsigned_t>(
                        (std::numeric_limits<int>::max)()))
                    return false;
                message_.value = static_cast<int>(value);
                message_.hasValue = true;
                break;
            default:
                return false;
            }
            finishValue();
            return true;
        }

        bool number_float(number_float_t, const string_t&) override {
            return false;
        }

        bool string(string_t& value) override {
            if (!beginValue() || field_ != Field::Type)
                return false;
            message_.type = value;
            finishValue();
            return true;
        }

        bool binary(binary_t&) override { return false; }

        bool start_object(std::size_t) override {
            if (rootStarted_ || rootEnded_ || depth_ != 0)
                return false;
            rootStarted_ = true;
            depth_ = 1;
            return true;
        }

        bool key(string_t& value) override {
            if (depth_ != 1 || rootEnded_ || field_ != Field::None)
                return false;

            Field next = Field::None;
            std::uint8_t bit = 0;
            if (value == "sequence") {
                next = Field::Sequence; bit = 1u << 0;
            } else if (value == "type") {
                next = Field::Type; bit = 1u << 1;
            } else if (value == "track") {
                next = Field::Track; bit = 1u << 2;
            } else if (value == "value") {
                next = Field::Value; bit = 1u << 3;
            } else if (value == "flag") {
                next = Field::Flag; bit = 1u << 4;
            } else {
                return false;
            }

            if ((seenFields_ & bit) != 0)
                return false;
            seenFields_ = static_cast<std::uint8_t>(seenFields_ | bit);
            field_ = next;
            return true;
        }

        bool end_object() override {
            if (depth_ != 1 || field_ != Field::None || rootEnded_)
                return false;
            depth_ = 0;
            rootEnded_ = true;
            return true;
        }

        bool start_array(std::size_t) override { return false; }
        bool end_array() override { return false; }

        bool parse_error(
            std::size_t, const string_t&, const nlohmann::detail::exception&) override {
            return false;
        }

        bool complete() const noexcept {
            return rootStarted_ && rootEnded_ && depth_ == 0 &&
                field_ == Field::None;
        }

        bool makeMessage(NativeUiCommandMessage& output) const {
            if (!complete() || (seenFields_ & 0x03u) != 0x03u)
                return false;

            const bool trackFaderOrPan =
                message_.type == "SetTrackFader" ||
                message_.type == "SetTrackPan";
            const bool trackMuteOrSolo =
                message_.type == "SetTrackMute" ||
                message_.type == "SetTrackSolo";
            const bool masterFader = message_.type == "SetMasterFader";
            const bool masterMute = message_.type == "SetMasterMute";

            if (trackFaderOrPan) {
                if (!message_.hasTrack || !message_.hasValue || message_.hasFlag)
                    return false;
            } else if (trackMuteOrSolo) {
                if (!message_.hasTrack || message_.hasValue || !message_.hasFlag)
                    return false;
            } else if (masterFader) {
                if (message_.hasTrack || !message_.hasValue || message_.hasFlag)
                    return false;
            } else if (masterMute) {
                if (message_.hasTrack || message_.hasValue || !message_.hasFlag)
                    return false;
            } else {
                return false;
            }

            output = message_;
            return true;
        }

    private:
        enum class Field : std::uint8_t {
            None, Sequence, Type, Track, Value, Flag
        };

        bool beginValue() const noexcept {
            return rootStarted_ && !rootEnded_ && depth_ == 1 &&
                field_ != Field::None;
        }

        void finishValue() noexcept {
            field_ = Field::None;
        }

        NativeUiCommandMessage message_;
        Field field_ = Field::None;
        std::uint8_t seenFields_ = 0;
        std::size_t depth_ = 0;
        bool rootStarted_ = false;
        bool rootEnded_ = false;
    };
};

} // namespace mpcmixer
