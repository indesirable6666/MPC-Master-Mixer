#pragma once

#include "../../third_party/nlohmann/json.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace mpcmixer {

enum class NativeUiProtocolV1ClientMessageKind : std::uint8_t {
    Hello,
    SnapshotRequest
};

struct NativeUiProtocolV1ClientMessage {
    NativeUiProtocolV1ClientMessageKind kind =
        NativeUiProtocolV1ClientMessageKind::Hello;
    std::uint64_t epoch = 0;
    std::uint64_t request = 0;
    std::string client;
};

enum class NativeUiProtocolV1ClientDecodeStatus : std::uint8_t {
    Ok,
    Empty,
    TooLarge,
    InvalidJson,
    InvalidMessage
};

class NativeUiProtocolV1ClientMessageDecoder {
public:
    static constexpr std::size_t kMaximumPayloadBytes = 1024;
    static constexpr std::size_t kMaximumClientBytes = 64;

    static NativeUiProtocolV1ClientDecodeStatus decode(
        const std::string& input,
        NativeUiProtocolV1ClientMessage& output) {
        if (input.empty())
            return NativeUiProtocolV1ClientDecodeStatus::Empty;
        if (input.size() > kMaximumPayloadBytes)
            return NativeUiProtocolV1ClientDecodeStatus::TooLarge;

        Handler handler;
        const bool parsed = nlohmann::json::sax_parse(
            input, &handler, nlohmann::json::input_format_t::json, true, false);
        if (!parsed || !handler.complete())
            return NativeUiProtocolV1ClientDecodeStatus::InvalidJson;

        NativeUiProtocolV1ClientMessage candidate;
        if (!handler.build(candidate))
            return NativeUiProtocolV1ClientDecodeStatus::InvalidMessage;

        output = candidate;
        return NativeUiProtocolV1ClientDecodeStatus::Ok;
    }

private:
    class Handler : public nlohmann::json_sax<nlohmann::json> {
        enum class Field : std::uint8_t {
            None, Version, Kind, Epoch, Client, Request
        };

    public:
        bool null() override { return false; }
        bool boolean(bool) override { return false; }

        bool number_integer(number_integer_t value) override {
            if (value < 0)
                return false;
            return number_unsigned(static_cast<number_unsigned_t>(value));
        }

        bool number_unsigned(number_unsigned_t value) override {
            if (!valueReady())
                return false;

            switch (field_) {
            case Field::Version:
                if (value != 1)
                    return false;
                versionSeen_ = true;
                break;
            case Field::Epoch:
                epoch_ = static_cast<std::uint64_t>(value);
                epochSeen_ = true;
                break;
            case Field::Request:
                if (value == 0)
                    return false;
                request_ = static_cast<std::uint64_t>(value);
                requestSeen_ = true;
                break;
            default:
                return false;
            }

            field_ = Field::None;
            return true;
        }

        bool number_float(number_float_t, const string_t&) override {
            return false;
        }

        bool string(string_t& value) override {
            if (!valueReady())
                return false;

            if (field_ == Field::Kind) {
                if (value != "hello" && value != "snapshot_request")
                    return false;
                kind_ = value;
                kindSeen_ = true;
            } else if (field_ == Field::Client) {
                if (value.empty() || value.size() > kMaximumClientBytes)
                    return false;
                client_ = value;
                clientSeen_ = true;
            } else {
                return false;
            }

            field_ = Field::None;
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

            std::uint8_t bit = 0;
            if (value == "version") {
                field_ = Field::Version;
                bit = 1u << 0;
            } else if (value == "kind") {
                field_ = Field::Kind;
                bit = 1u << 1;
            } else if (value == "epoch") {
                field_ = Field::Epoch;
                bit = 1u << 2;
            } else if (value == "client") {
                field_ = Field::Client;
                bit = 1u << 3;
            } else if (value == "request") {
                field_ = Field::Request;
                bit = 1u << 4;
            } else {
                return false;
            }

            if ((seen_ & bit) != 0)
                return false;
            seen_ = static_cast<std::uint8_t>(seen_ | bit);
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
            std::size_t,
            const string_t&,
            const nlohmann::detail::exception&) override {
            return false;
        }

        bool complete() const noexcept {
            return rootStarted_ && rootEnded_ && depth_ == 0 &&
                field_ == Field::None;
        }

        bool build(NativeUiProtocolV1ClientMessage& output) const {
            if (!complete() || !versionSeen_ || !kindSeen_ || !epochSeen_)
                return false;

            if (kind_ == "hello") {
                if (epoch_ != 0 || !clientSeen_ || requestSeen_ ||
                    seen_ != 0x0fu)
                    return false;

                output.kind = NativeUiProtocolV1ClientMessageKind::Hello;
                output.epoch = 0;
                output.client = client_;
                return true;
            }

            if (kind_ == "snapshot_request") {
                if (epoch_ == 0 || !requestSeen_ || clientSeen_ ||
                    seen_ != 0x17u)
                    return false;

                output.kind =
                    NativeUiProtocolV1ClientMessageKind::SnapshotRequest;
                output.epoch = epoch_;
                output.request = request_;
                return true;
            }

            return false;
        }

    private:
        bool valueReady() const noexcept {
            return rootStarted_ && !rootEnded_ && depth_ == 1 &&
                field_ != Field::None;
        }

        Field field_ = Field::None;
        std::uint8_t seen_ = 0;
        std::uint64_t epoch_ = 0;
        std::uint64_t request_ = 0;
        std::string kind_;
        std::string client_;
        std::size_t depth_ = 0;
        bool rootStarted_ = false;
        bool rootEnded_ = false;
        bool versionSeen_ = false;
        bool kindSeen_ = false;
        bool epochSeen_ = false;
        bool clientSeen_ = false;
        bool requestSeen_ = false;
    };
};

} // namespace mpcmixer
