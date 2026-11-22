#include "native_ui_protocol_v1_snapshot_decoder.hpp"
#include "native_ui_protocol_v1_snapshot_serializer.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

using namespace mpcmixer;
using Json = nlohmann::json;

int main() {
    MixerPresentationSnapshot source;
    const auto defaults = SixBandEq::defaultBandParameters();
    for (auto& track : source.tracks) track.eq = defaults;
    for (auto& channel : source.audioInputPair.channels) channel.eq = defaults;
    source.sequence = 91;
    source.audioTicketWatermark = 1234;
    source.audioStateCaughtUp = false;
    source.tracks[0].faderValue = 73;
    source.tracks[0].panPercent = -35;
    source.tracks[0].mute = true;
    source.tracks[0].auxSendPercent[3] = 42;
    source.tracks[0].eqConfigured = true;
    source.tracks[0].eqSampleRate = 48000.0f;
    source.tracks[0].eq[2].frequencyHz = 850.0f;
    source.tracks[0].eq[2].gainDb = 5.5f;
    source.tracks[0].hasInsert[1] = true;
    source.auxReturns[2].levelValue = 64;
    source.auxReturns[2].balancePercent = -30;
    source.auxReturns[2].mute = true;
    source.masterFaderValue = 88;
    source.masterMute = true;
    source.audioInputPair.linked = true;
    source.audioInputPair.stereoGainValue = 62;
    source.audioInputPair.balancePercent = -25;
    source.audioInputPair.stereoAuxSendPercent[1] = 25;
    source.audioInputPair.channels[0].gainValue = 62;
    source.audioInputPair.channels[0].panPercent = -45;
    source.audioInputPair.channels[1].eqSampleRate = 44100.0f;

    std::string wire;
    assert(NativeUiProtocolV1SnapshotSerializer::serialize(7, 3, source, wire));
    MixerPresentationSnapshot decoded;
    assert(NativeUiProtocolV1SnapshotDecoder::decode(wire, 7, 3, decoded) ==
        NativeUiProtocolV1SnapshotDecodeStatus::Ok);
    assert(decoded.sequence == source.sequence);
    assert(decoded.audioTicketWatermark == source.audioTicketWatermark);
    assert(!decoded.audioStateCaughtUp);
    assert(decoded.tracks[0].faderValue == 73);
    assert(decoded.tracks[0].panPercent == -35);
    assert(decoded.tracks[0].mute);
    assert(decoded.tracks[0].auxSendPercent[3] == 42);
    assert(decoded.tracks[0].eqConfigured);
    assert(decoded.tracks[0].eqSampleRate == 48000.0f);
    assert(decoded.tracks[0].eq[2].frequencyHz == 850.0f);
    assert(decoded.tracks[0].hasInsert[1]);
    assert(decoded.auxReturns[2].levelValue == 64);
    assert(decoded.auxReturns[2].balancePercent == -30);
    assert(decoded.masterFaderValue == 88 && decoded.masterMute);
    assert(decoded.audioInputPair.linked);
    assert(decoded.audioInputPair.channels[0].gainValue == 62);
    assert(decoded.audioInputPair.channels[0].panPercent == -45);

    MixerPresentationSnapshot boundary = source;
    boundary.sequence = (std::numeric_limits<std::uint64_t>::max)();
    boundary.audioTicketWatermark = (std::numeric_limits<std::uint32_t>::max)();
    std::string boundaryWire;
    const auto uint64Max = (std::numeric_limits<std::uint64_t>::max)();
    assert(NativeUiProtocolV1SnapshotSerializer::serialize(
        uint64Max, uint64Max, boundary, boundaryWire));
    MixerPresentationSnapshot boundaryDecoded;
    assert(NativeUiProtocolV1SnapshotDecoder::decode(
        boundaryWire, uint64Max, uint64Max, boundaryDecoded) ==
        NativeUiProtocolV1SnapshotDecodeStatus::Ok);
    assert(boundaryDecoded.sequence == uint64Max);
    assert(boundaryDecoded.audioTicketWatermark ==
        (std::numeric_limits<std::uint32_t>::max)());

    const auto reject = [&decoded, &wire](const std::string& payload,
                                          std::uint64_t epoch = 7,
                                          std::uint64_t request = 3) {
        const auto oldSequence = decoded.sequence;
        const auto oldFader = decoded.tracks[0].faderValue;
        assert(NativeUiProtocolV1SnapshotDecoder::decode(
            payload, epoch, request, decoded) !=
            NativeUiProtocolV1SnapshotDecodeStatus::Ok);
        assert(decoded.sequence == oldSequence);
        assert(decoded.tracks[0].faderValue == oldFader);
    };

    Json base = Json::parse(wire);
    Json bad = base;
    bad["extra"] = 1;
    reject(bad.dump());
    bad = base;
    bad["state"]["tracks"].erase(0);
    reject(bad.dump());
    bad = base;
    bad["state"]["tracks"][0]["aux_sends"].push_back(1);
    reject(bad.dump());
    bad = base;
    bad["state"]["tracks"][0]["fader"] = 1.0;
    reject(bad.dump());
    bad = base;
    bad["state"]["tracks"][0]["pan"] = 101;
    reject(bad.dump());
    bad = base;
    bad["state"]["tracks"][0]["eq"][0]["gain_db"] = 13.0;
    reject(bad.dump());
    bad = base;
    bad["state"]["tracks"][0]["eq"][0]["frequency_hz"] = 24000.0;
    reject(bad.dump());
    bad = base;
    bad["state"]["tracks"][0]["eq_configured"] = false;
    bad["state"]["tracks"][0]["eq_sample_rate"] = 48000.0;
    reject(bad.dump());
    bad = base;
    bad["state"]["audio_input_pair"]["channels"][0]["has_insert"].push_back(false);
    reject(bad.dump());
    bad = base;
    bad["epoch"] = 8;
    reject(bad.dump());
    reject(wire, 8, 3);
    reject(wire, 7, 4);
    reject(wire + "{}", 7, 3);
    reject("{", 7, 3);

    std::string duplicate = wire;
    const std::string needle = "\"version\":1";
    const auto position = duplicate.find(needle);
    assert(position != std::string::npos);
    duplicate.insert(position + needle.size(), ",\"version\":1");
    reject(duplicate);

    assert(NativeUiProtocolV1SnapshotDecoder::decode(
        "", 7, 3, decoded) == NativeUiProtocolV1SnapshotDecodeStatus::Empty);
    std::string oversized(
        NativeUiProtocolV1SnapshotDecoder::kMaximumPayloadBytes + 1, ' ');
    assert(NativeUiProtocolV1SnapshotDecoder::decode(
        oversized, 7, 3, decoded) ==
        NativeUiProtocolV1SnapshotDecodeStatus::TooLarge);
    assert(NativeUiProtocolV1SnapshotDecoder::decode(
        wire, 0, 3, decoded) ==
        NativeUiProtocolV1SnapshotDecodeStatus::InvalidMessage);

    return 0;
}
