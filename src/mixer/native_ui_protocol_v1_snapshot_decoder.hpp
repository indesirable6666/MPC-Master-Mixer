#pragma once

#include "mixer_presentation_snapshot.hpp"
#include "../../third_party/nlohmann/json.hpp"

#include <cmath>
#include <cstddef>
#include <cstdint>
#include <map>
#include <limits>
#include <set>
#include <string>
#include <stdexcept>

namespace mpcmixer {

enum class NativeUiProtocolV1SnapshotDecodeStatus : std::uint8_t {
    Ok, Empty, TooLarge, InvalidJson, InvalidMessage
};

// Decodes one complete v1 snapshot payload. The caller supplies the active
// connection epoch and outstanding request; neither is inferred from the wire.
// Parsing and allocation belong on the serialized control/transport thread.
class NativeUiProtocolV1SnapshotDecoder {
public:
    static constexpr std::size_t kMaximumPayloadBytes = 32768;

    static NativeUiProtocolV1SnapshotDecodeStatus decode(
        const std::string& input, std::uint64_t expectedEpoch,
        std::uint64_t expectedRequest, MixerPresentationSnapshot& output) {
        if (input.empty()) return NativeUiProtocolV1SnapshotDecodeStatus::Empty;
        if (input.size() > kMaximumPayloadBytes)
            return NativeUiProtocolV1SnapshotDecodeStatus::TooLarge;
        if (expectedEpoch == 0 || expectedRequest == 0)
            return NativeUiProtocolV1SnapshotDecodeStatus::InvalidMessage;

        try {
            DuplicateKeyGuard guard;
            const auto callback = [&guard](int depth, nlohmann::json::parse_event_t event,
                                            nlohmann::json& parsed) {
                return guard.accept(depth, event, parsed);
            };
            const nlohmann::json message = nlohmann::json::parse(
                input, callback, true, false);
            MixerPresentationSnapshot candidate;
            if (!decodeMessage(message, expectedEpoch, expectedRequest, candidate))
                return NativeUiProtocolV1SnapshotDecodeStatus::InvalidMessage;
            output = candidate;
            return NativeUiProtocolV1SnapshotDecodeStatus::Ok;
        } catch (...) {
            return NativeUiProtocolV1SnapshotDecodeStatus::InvalidJson;
        }
    }

private:
    class DuplicateKeyGuard {
    public:
        bool accept(int depth, nlohmann::json::parse_event_t event,
                    nlohmann::json& parsed) {
            (void)depth;
            if (event == nlohmann::json::parse_event_t::object_start) {
                keys_.emplace_back();
            } else if (event == nlohmann::json::parse_event_t::key) {
                if (keys_.empty() ||
                    !keys_.back().insert(parsed.get<std::string>()).second)
                    throw std::runtime_error("duplicate JSON key");
            } else if (event == nlohmann::json::parse_event_t::object_end) {
                if (keys_.empty()) throw std::runtime_error("invalid object scope");
                keys_.pop_back();
            }
            return true;
        }
    private:
        std::vector<std::set<std::string>> keys_;
    };

    static bool fields(const nlohmann::json& value,
                       std::initializer_list<const char*> names) {
        if (!value.is_object() || value.size() != names.size()) return false;
        for (const char* name : names)
            if (value.find(name) == value.end()) return false;
        return true;
    }

    static bool unsignedValue(const nlohmann::json& value,
                              std::uint64_t maximum, std::uint64_t& output) {
        if (value.is_number_unsigned()) {
            const auto number = value.get<std::uint64_t>();
            if (number > maximum) return false;
            output = number;
            return true;
        }
        if (value.is_number_integer()) {
            const auto number = value.get<std::int64_t>();
            if (number < 0 || static_cast<std::uint64_t>(number) > maximum)
                return false;
            output = static_cast<std::uint64_t>(number);
            return true;
        }
        return false;
    }

    static bool signedValue(const nlohmann::json& value, int minimum,
                            int maximum, int& output) {
        if (!value.is_number_integer()) return false;
        if (value.is_number_unsigned()) {
            const auto number = value.get<std::uint64_t>();
            if (number > static_cast<std::uint64_t>(maximum)) return false;
            output = static_cast<int>(number);
        } else {
            const auto number = value.get<std::int64_t>();
            if (number < minimum || number > maximum) return false;
            output = static_cast<int>(number);
        }
        return output >= minimum && output <= maximum;
    }

    static bool floatValue(const nlohmann::json& value, float& output) {
        if (!value.is_number()) return false;
        const double number = value.get<double>();
        if (!std::isfinite(number) ||
            number > (std::numeric_limits<float>::max)() ||
            number < -(std::numeric_limits<float>::max)()) return false;
        output = static_cast<float>(number);
        return std::isfinite(output);
    }

    static bool booleanValue(const nlohmann::json& value, bool& output) {
        if (!value.is_boolean()) return false;
        output = value.get<bool>();
        return true;
    }

    template <std::size_t N>
    static bool byteArray(const nlohmann::json& value,
                          std::array<std::uint8_t, N>& output,
                          std::uint8_t maximum) {
        if (!value.is_array() || value.size() != N) return false;
        for (std::size_t i = 0; i < N; ++i) {
            std::uint64_t number = 0;
            if (!unsignedValue(value[i], maximum, number)) return false;
            output[i] = static_cast<std::uint8_t>(number);
        }
        return true;
    }

    template <std::size_t N>
    static bool boolArray(const nlohmann::json& value,
                          std::array<bool, N>& output) {
        if (!value.is_array() || value.size() != N) return false;
        for (std::size_t i = 0; i < N; ++i)
            if (!booleanValue(value[i], output[i])) return false;
        return true;
    }

    static bool decodeEq(const nlohmann::json& value,
        std::array<SixBandEq::BandParameters,
                   MixerPresentationSnapshot::kEqBandCount>& output,
        float sampleRate, bool checkNyquist) {
        if (!value.is_array() || value.size() != MixerPresentationSnapshot::kEqBandCount)
            return false;
        for (std::size_t i = 0; i < output.size(); ++i) {
            const auto& band = value[i];
            if (!fields(band, {"frequency_hz", "q", "gain_db"}) ||
                !floatValue(band["frequency_hz"], output[i].frequencyHz) ||
                !floatValue(band["q"], output[i].q) ||
                !floatValue(band["gain_db"], output[i].gainDb)) return false;
            if (output[i].frequencyHz < SixBandEq::kMinFrequencyHz ||
                (checkNyquist && output[i].frequencyHz >= sampleRate * 0.5f) ||
                output[i].q < SixBandEq::kMinQ ||
                output[i].q > SixBandEq::kMaxQ ||
                output[i].gainDb < SixBandEq::kMinGainDb ||
                output[i].gainDb > SixBandEq::kMaxGainDb) return false;
        }
        return true;
    }

    static bool decodeTrack(const nlohmann::json& value,
                            MixerPresentationSnapshot::Track& track) {
        if (!fields(value, {"fader", "pan", "mute", "solo", "aux_sends",
                            "eq", "eq_configured", "eq_sample_rate", "has_insert"}))
            return false;
        std::uint64_t fader = 0;
        int pan = 0;
        if (!unsignedValue(value["fader"], 127, fader) ||
            !signedValue(value["pan"], -100, 100, pan) ||
            !booleanValue(value["mute"], track.mute) ||
            !booleanValue(value["solo"], track.solo) ||
            !byteArray(value["aux_sends"], track.auxSendPercent, 100) ||
            !booleanValue(value["eq_configured"], track.eqConfigured) ||
            !floatValue(value["eq_sample_rate"], track.eqSampleRate) ||
            !boolArray(value["has_insert"], track.hasInsert)) return false;
        track.faderValue = static_cast<std::uint8_t>(fader);
        track.panPercent = static_cast<std::int16_t>(pan);
        if (track.eqConfigured) {
            if (track.eqSampleRate < 8000.0f || track.eqSampleRate > 192000.0f)
                return false;
        } else if (track.eqSampleRate != 0.0f) return false;
        return decodeEq(value["eq"], track.eq, track.eqSampleRate,
                        track.eqConfigured);
    }

    static bool decodeAux(const nlohmann::json& value,
                          MixerPresentationSnapshot::AuxReturn& aux) {
        if (!fields(value, {"level", "balance", "mute"})) return false;
        std::uint64_t level = 0;
        int balance = 0;
        if (!unsignedValue(value["level"], 127, level) ||
            !signedValue(value["balance"], -100, 100, balance) ||
            !booleanValue(value["mute"], aux.mute)) return false;
        aux.levelValue = static_cast<std::uint8_t>(level);
        aux.balancePercent = static_cast<std::int16_t>(balance);
        return true;
    }

    static bool decodeChannel(const nlohmann::json& value,
        MixerPresentationSnapshot::AudioInputChannel& channel) {
        if (!fields(value, {"gain", "pan", "mute", "solo", "eq_sample_rate",
                            "aux_sends", "eq", "has_insert"})) return false;
        std::uint64_t gain = 0;
        int pan = 0;
        if (!unsignedValue(value["gain"], 127, gain) ||
            !signedValue(value["pan"], -100, 100, pan) ||
            !booleanValue(value["mute"], channel.mute) ||
            !booleanValue(value["solo"], channel.solo) ||
            !floatValue(value["eq_sample_rate"], channel.eqSampleRate) ||
            !byteArray(value["aux_sends"], channel.auxSendPercent, 100) ||
            !boolArray(value["has_insert"], channel.hasInsert) ||
            channel.eqSampleRate < 8000.0f || channel.eqSampleRate > 192000.0f)
            return false;
        channel.gainValue = static_cast<std::uint8_t>(gain);
        channel.panPercent = static_cast<std::int16_t>(pan);
        return decodeEq(value["eq"], channel.eq, channel.eqSampleRate, true);
    }

    static bool decodeAudioInput(const nlohmann::json& value,
        MixerPresentationSnapshot::AudioInputPair& pair) {
        if (!fields(value, {"linked", "stereo_gain", "balance", "stereo_mute",
                            "stereo_solo", "stereo_aux_sends", "channels"}))
            return false;
        std::uint64_t gain = 0;
        int balance = 0;
        if (!booleanValue(value["linked"], pair.linked) ||
            !unsignedValue(value["stereo_gain"], 127, gain) ||
            !signedValue(value["balance"], -100, 100, balance) ||
            !booleanValue(value["stereo_mute"], pair.stereoMute) ||
            !booleanValue(value["stereo_solo"], pair.stereoSolo) ||
            !byteArray(value["stereo_aux_sends"], pair.stereoAuxSendPercent, 100) ||
            !value["channels"].is_array() || value["channels"].size() != 2)
            return false;
        pair.stereoGainValue = static_cast<std::uint8_t>(gain);
        pair.balancePercent = static_cast<std::int16_t>(balance);
        return decodeChannel(value["channels"][0], pair.channels[0]) &&
               decodeChannel(value["channels"][1], pair.channels[1]);
    }

    static bool decodeMessage(const nlohmann::json& message,
        std::uint64_t expectedEpoch, std::uint64_t expectedRequest,
        MixerPresentationSnapshot& snapshot) {
        if (!fields(message, {"version", "kind", "epoch", "request", "sequence",
                              "audio_ticket_watermark", "audio_state_caught_up", "state"}))
            return false;
        std::uint64_t version = 0, epoch = 0, request = 0, sequence = 0, watermark = 0;
        if (!unsignedValue(message["version"], 1, version) || version != 1 ||
            !message["kind"].is_string() || message["kind"] != "snapshot" ||
            !unsignedValue(message["epoch"], (std::numeric_limits<std::uint64_t>::max)(), epoch) ||
            epoch != expectedEpoch ||
            !unsignedValue(message["request"], (std::numeric_limits<std::uint64_t>::max)(), request) ||
            request != expectedRequest ||
            !unsignedValue(message["sequence"], (std::numeric_limits<std::uint64_t>::max)(), sequence) ||
            sequence == 0 ||
            !unsignedValue(message["audio_ticket_watermark"],
                           (std::numeric_limits<std::uint32_t>::max)(), watermark) ||
            !booleanValue(message["audio_state_caught_up"], snapshot.audioStateCaughtUp))
            return false;
        const auto& state = message["state"];
        if (!fields(state, {"tracks", "aux_returns", "master_fader", "master_mute",
                            "audio_input_pair"}) ||
            !state["tracks"].is_array() ||
            state["tracks"].size() != MixerPresentationSnapshot::kTrackCount ||
            !state["aux_returns"].is_array() ||
            state["aux_returns"].size() != MixerPresentationSnapshot::kAuxBusCount)
            return false;
        snapshot.sequence = sequence;
        snapshot.audioTicketWatermark = static_cast<std::uint32_t>(watermark);
        std::uint64_t master = 0;
        if (!unsignedValue(state["master_fader"], 127, master) ||
            !booleanValue(state["master_mute"], snapshot.masterMute) ||
            !decodeAudioInput(state["audio_input_pair"], snapshot.audioInputPair))
            return false;
        snapshot.masterFaderValue = static_cast<std::uint8_t>(master);
        for (std::size_t i = 0; i < snapshot.tracks.size(); ++i)
            if (!decodeTrack(state["tracks"][i], snapshot.tracks[i])) return false;
        for (std::size_t i = 0; i < snapshot.auxReturns.size(); ++i)
            if (!decodeAux(state["aux_returns"][i], snapshot.auxReturns[i])) return false;
        return true;
    }
};

} // namespace mpcmixer
