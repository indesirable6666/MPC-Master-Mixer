#pragma once

#include "mixer_presentation_snapshot.hpp"
#include "../../third_party/nlohmann/json.hpp"

#include <cmath>
#include <cstdint>
#include <string>

namespace mpcmixer {

// Full snapshots are bounded independently from the 1024-byte command limit.
// 32 KiB leaves room for all 16 tracks, 6 EQ bands per strip, Audio In and
// metadata while keeping one-frame allocation and queue costs bounded.
class NativeUiProtocolV1SnapshotSerializer {
public:
    static constexpr std::size_t kMaximumPayloadBytes = 32768;

    static bool serialize(std::uint64_t epoch,
                          std::uint64_t request,
                          const MixerPresentationSnapshot& snapshot,
                          std::string& output) {
        if (epoch == 0 || request == 0 || snapshot.sequence == 0 ||
            !valid(snapshot)) return false;

        try {
            nlohmann::json state = nlohmann::json::object();
            state["tracks"] = nlohmann::json::array();
            for (const auto& track : snapshot.tracks)
                state["tracks"].push_back(trackJson(track));

            state["aux_returns"] = nlohmann::json::array();
            for (const auto& aux : snapshot.auxReturns) {
                nlohmann::json item = nlohmann::json::object();
                item["level"] = aux.levelValue;
                item["balance"] = aux.balancePercent;
                item["mute"] = aux.mute;
                state["aux_returns"].push_back(item);
            }
            state["master_fader"] = snapshot.masterFaderValue;
            state["master_mute"] = snapshot.masterMute;
            state["audio_input_pair"] = audioInputJson(snapshot.audioInputPair);

            nlohmann::json message = nlohmann::json::object();
            message["version"] = 1;
            message["kind"] = "snapshot";
            message["epoch"] = epoch;
            message["request"] = request;
            message["sequence"] = snapshot.sequence;
            message["audio_ticket_watermark"] = snapshot.audioTicketWatermark;
            message["audio_state_caught_up"] = snapshot.audioStateCaughtUp;
            message["state"] = state;

            const std::string candidate = message.dump(
                -1, ' ', false, nlohmann::json::error_handler_t::strict);
            if (candidate.empty() || candidate.size() > kMaximumPayloadBytes)
                return false;
            output = candidate;
            return true;
        } catch (...) {
            return false;
        }
    }

private:
    static nlohmann::json eqJson(
        const std::array<SixBandEq::BandParameters,
                         MixerPresentationSnapshot::kEqBandCount>& bands) {
        nlohmann::json result = nlohmann::json::array();
        for (const auto& band : bands) {
            nlohmann::json item = nlohmann::json::object();
            item["frequency_hz"] = band.frequencyHz;
            item["q"] = band.q;
            item["gain_db"] = band.gainDb;
            result.push_back(item);
        }
        return result;
    }

    static nlohmann::json boolArray(
        const std::array<bool, MixerPresentationSnapshot::kInsertSlotCount>& values) {
        nlohmann::json result = nlohmann::json::array();
        for (bool value : values) result.push_back(value);
        return result;
    }

    static nlohmann::json trackJson(
        const MixerPresentationSnapshot::Track& track) {
        nlohmann::json item = nlohmann::json::object();
        item["fader"] = track.faderValue;
        item["pan"] = track.panPercent;
        item["mute"] = track.mute;
        item["solo"] = track.solo;
        item["aux_sends"] = track.auxSendPercent;
        item["eq"] = eqJson(track.eq);
        item["eq_configured"] = track.eqConfigured;
        item["eq_sample_rate"] = track.eqSampleRate;
        item["has_insert"] = boolArray(track.hasInsert);
        return item;
    }

    static nlohmann::json audioInputJson(
        const MixerPresentationSnapshot::AudioInputPair& pair) {
        nlohmann::json item = nlohmann::json::object();
        item["linked"] = pair.linked;
        item["stereo_gain"] = pair.stereoGainValue;
        item["balance"] = pair.balancePercent;
        item["stereo_mute"] = pair.stereoMute;
        item["stereo_solo"] = pair.stereoSolo;
        item["stereo_aux_sends"] = pair.stereoAuxSendPercent;
        item["channels"] = nlohmann::json::array();
        for (const auto& channel : pair.channels) {
            nlohmann::json side = nlohmann::json::object();
            side["gain"] = channel.gainValue;
            side["pan"] = channel.panPercent;
            side["mute"] = channel.mute;
            side["solo"] = channel.solo;
            side["eq_sample_rate"] = channel.eqSampleRate;
            side["aux_sends"] = channel.auxSendPercent;
            side["eq"] = eqJson(channel.eq);
            side["has_insert"] = boolArray(channel.hasInsert);
            item["channels"].push_back(side);
        }
        return item;
    }

    static bool validEq(
        const std::array<SixBandEq::BandParameters,
                         MixerPresentationSnapshot::kEqBandCount>& bands,
        float sampleRate) {
        if (!std::isfinite(sampleRate) || sampleRate < 8000.0f ||
            sampleRate > 192000.0f) return false;
        for (const auto& band : bands) {
            if (!std::isfinite(band.frequencyHz) ||
                !std::isfinite(band.q) || !std::isfinite(band.gainDb) ||
                band.frequencyHz < SixBandEq::kMinFrequencyHz ||
                band.frequencyHz >= sampleRate * 0.5f ||
                band.q < SixBandEq::kMinQ || band.q > SixBandEq::kMaxQ ||
                band.gainDb < SixBandEq::kMinGainDb ||
                band.gainDb > SixBandEq::kMaxGainDb) return false;
        }
        return true;
    }

    static bool valid(const MixerPresentationSnapshot& snapshot) {
        if (snapshot.masterFaderValue > 127) return false;
        for (const auto& track : snapshot.tracks) {
            if (track.faderValue > 127 || track.panPercent < -100 ||
                track.panPercent > 100) return false;
            for (auto send : track.auxSendPercent)
                if (send > 100) return false;
            if (track.eqConfigured) {
                if (!validEq(track.eq, track.eqSampleRate)) return false;
            } else {
                if (track.eqSampleRate != 0.0f) return false;
                for (const auto& band : track.eq) {
                    if (!std::isfinite(band.frequencyHz) ||
                        !std::isfinite(band.q) || !std::isfinite(band.gainDb) ||
                        band.frequencyHz < SixBandEq::kMinFrequencyHz ||
                        band.q < SixBandEq::kMinQ || band.q > SixBandEq::kMaxQ ||
                        band.gainDb < SixBandEq::kMinGainDb ||
                        band.gainDb > SixBandEq::kMaxGainDb) return false;
                }
            }
        }
        for (const auto& aux : snapshot.auxReturns)
            if (aux.levelValue > 127 || aux.balancePercent < -100 ||
                aux.balancePercent > 100) return false;

        const auto& pair = snapshot.audioInputPair;
        if (pair.stereoGainValue > 127 || pair.balancePercent < -100 ||
            pair.balancePercent > 100) return false;
        for (auto send : pair.stereoAuxSendPercent)
            if (send > 100) return false;
        for (const auto& channel : pair.channels) {
            if (channel.gainValue > 127 || channel.panPercent < -100 ||
                channel.panPercent > 100) return false;
            for (auto send : channel.auxSendPercent)
                if (send > 100) return false;
            if (!validEq(channel.eq, channel.eqSampleRate)) return false;
        }
        return true;
    }
};

} // namespace mpcmixer
