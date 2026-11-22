#include "native_ui_protocol_v1_snapshot_serializer.hpp"

#include <cassert>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>

using namespace mpcmixer;
using Json = nlohmann::json;

static void setWorstEq(
    std::array<SixBandEq::BandParameters, MixerPresentationSnapshot::kEqBandCount>& bands) {
    for (auto& band : bands) {
        band.frequencyHz = 95999.0f;
        band.q = 10.0f;
        band.gainDb = 12.0f;
    }
}

int main() {
    MixerPresentationSnapshot snapshot;
    const auto defaultBands = SixBandEq::defaultBandParameters();
    for (auto& track : snapshot.tracks) track.eq = defaultBands;
    for (auto& channel : snapshot.audioInputPair.channels)
        channel.eq = defaultBands;
    snapshot.sequence = 12;
    snapshot.audioTicketWatermark = 77;
    snapshot.audioStateCaughtUp = false;
    snapshot.tracks[0].faderValue = 73;
    snapshot.tracks[0].panPercent = -35;
    snapshot.tracks[0].mute = true;
    snapshot.tracks[0].solo = true;
    snapshot.tracks[0].auxSendPercent[3] = 42;
    snapshot.tracks[0].eqConfigured = true;
    snapshot.tracks[0].eqSampleRate = 48000.0f;
    snapshot.tracks[0].eq[2].frequencyHz = 850.0f;
    snapshot.tracks[0].eq[2].gainDb = 5.5f;
    snapshot.tracks[0].hasInsert[1] = true;
    snapshot.auxReturns[2].levelValue = 64;
    snapshot.auxReturns[2].balancePercent = -30;
    snapshot.auxReturns[2].mute = true;
    snapshot.masterFaderValue = 88;
    snapshot.masterMute = true;
    snapshot.audioInputPair.linked = true;
    snapshot.audioInputPair.stereoGainValue = 62;
    snapshot.audioInputPair.balancePercent = -25;
    snapshot.audioInputPair.stereoMute = true;
    snapshot.audioInputPair.stereoSolo = true;
    snapshot.audioInputPair.stereoAuxSendPercent[1] = 25;
    snapshot.audioInputPair.channels[0].gainValue = 62;
    snapshot.audioInputPair.channels[0].panPercent = -45;
    snapshot.audioInputPair.channels[0].eq[2].gainDb = 2.0f;
    snapshot.audioInputPair.channels[1].eqSampleRate = 44100.0f;

    std::string wire = "unchanged";
    assert(NativeUiProtocolV1SnapshotSerializer::serialize(7, 1, snapshot, wire));
    assert(wire.size() <= NativeUiProtocolV1SnapshotSerializer::kMaximumPayloadBytes);
    const Json j = Json::parse(wire);
    assert(j.size() == 8);
    assert(j["version"] == 1 && j["kind"] == "snapshot");
    assert(j["epoch"] == 7 && j["request"] == 1 && j["sequence"] == 12);
    assert(j["audio_ticket_watermark"] == 77);
    assert(j["audio_state_caught_up"] == false);
    const Json& state = j["state"];
    assert(state.size() == 5);
    assert(state["tracks"].size() == 16);
    assert(state["aux_returns"].size() == 4);
    assert(state["master_fader"] == 88 && state["master_mute"] == true);
    assert(state["tracks"][0].size() == 9);
    assert(state["tracks"][0]["fader"] == 73);
    assert(state["tracks"][0]["pan"] == -35);
    assert(state["tracks"][0]["aux_sends"].size() == 4);
    assert(state["tracks"][0]["eq"].size() == 6);
    assert(state["tracks"][0]["eq_configured"] == true);
    assert(state["tracks"][0]["eq_sample_rate"] == 48000.0);
    assert(state["tracks"][0]["has_insert"].size() == 4);
    assert(state["tracks"][0]["has_insert"][1] == true);
    assert(state["aux_returns"][2]["level"] == 64);
    assert(state["audio_input_pair"].size() == 7);
    assert(state["audio_input_pair"]["channels"].size() == 2);
    assert(state["audio_input_pair"]["channels"][1]["eq"].size() == 6);

    // The fixed maximum-size fixture exercises every repeated field at its
    // largest valid numeric values and records the measured payload size.
    MixerPresentationSnapshot maximum;
    maximum.sequence = (std::numeric_limits<std::uint64_t>::max)();
    maximum.audioTicketWatermark = (std::numeric_limits<std::uint32_t>::max)();
    maximum.audioStateCaughtUp = false;
    maximum.masterFaderValue = 127;
    maximum.masterMute = true;
    for (auto& track : maximum.tracks) {
        track.faderValue = 127;
        track.panPercent = 100;
        track.mute = true;
        track.solo = true;
        track.auxSendPercent.fill(100);
        track.eqConfigured = true;
        track.eqSampleRate = 192000.0f;
        setWorstEq(track.eq);
        track.hasInsert.fill(true);
    }
    for (auto& aux : maximum.auxReturns) {
        aux.levelValue = 127;
        aux.balancePercent = 100;
        aux.mute = true;
    }
    maximum.audioInputPair.linked = true;
    maximum.audioInputPair.stereoGainValue = 127;
    maximum.audioInputPair.balancePercent = 100;
    maximum.audioInputPair.stereoMute = true;
    maximum.audioInputPair.stereoSolo = true;
    maximum.audioInputPair.stereoAuxSendPercent.fill(100);
    for (auto& channel : maximum.audioInputPair.channels) {
        channel.gainValue = 127;
        channel.panPercent = 100;
        channel.mute = true;
        channel.solo = true;
        channel.eqSampleRate = 192000.0f;
        channel.auxSendPercent.fill(100);
        setWorstEq(channel.eq);
        channel.hasInsert.fill(true);
    }
    assert(NativeUiProtocolV1SnapshotSerializer::serialize(
        (std::numeric_limits<std::uint64_t>::max)(),
        (std::numeric_limits<std::uint64_t>::max)(), maximum, wire));
    assert(wire.size() <= NativeUiProtocolV1SnapshotSerializer::kMaximumPayloadBytes);
    assert(Json::parse(wire)["sequence"] ==
           (std::numeric_limits<std::uint64_t>::max)());

    const auto reject = [&wire](bool ok) {
        const std::string before = wire;
        assert(!ok);
        assert(wire == before);
    };
    reject(NativeUiProtocolV1SnapshotSerializer::serialize(0, 1, snapshot, wire));
    reject(NativeUiProtocolV1SnapshotSerializer::serialize(1, 0, snapshot, wire));
    MixerPresentationSnapshot noSequence = snapshot;
    noSequence.sequence = 0;
    reject(NativeUiProtocolV1SnapshotSerializer::serialize(1, 1, noSequence, wire));

    MixerPresentationSnapshot invalid = snapshot;
    invalid.tracks[0].eq[0].gainDb =
        (std::numeric_limits<float>::quiet_NaN)();
    reject(NativeUiProtocolV1SnapshotSerializer::serialize(1, 1, invalid, wire));
    invalid = snapshot;
    invalid.tracks[0].eqSampleRate = 0.0f;
    reject(NativeUiProtocolV1SnapshotSerializer::serialize(1, 1, invalid, wire));
    invalid = snapshot;
    invalid.audioInputPair.channels[0].eq[0].frequencyHz = 24000.0f;
    reject(NativeUiProtocolV1SnapshotSerializer::serialize(1, 1, invalid, wire));
    invalid = snapshot;
    invalid.tracks[0].auxSendPercent[0] = 101;
    reject(NativeUiProtocolV1SnapshotSerializer::serialize(1, 1, invalid, wire));
}
