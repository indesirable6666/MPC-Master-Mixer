#include "native_ui_protocol_v1_decoder.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

using namespace mpcmixer;

int main() {
    NativeUiProtocolV1Command decoded;
    const std::string valid =
        R"({"version":1,"kind":"command","epoch":7,"sequence":41,"type":"SetTrackFader","track":2,"value":96})";
    assert(NativeUiProtocolV1Decoder::decode(valid, decoded) ==
        NativeUiProtocolV1DecodeStatus::Ok);
    assert(decoded.epoch == 7 && decoded.command.sequence == 41);
    assert(decoded.command.type == "SetTrackFader");
    assert(decoded.command.hasTrack && decoded.command.track == 2);
    assert(decoded.command.hasValue && decoded.command.value == 96);

    const std::string negativePan =
        R"({"version":1,"kind":"command","epoch":7,"sequence":42,"type":"SetTrackPan","track":0,"value":-100})";
    assert(NativeUiProtocolV1Decoder::decode(negativePan, decoded) ==
        NativeUiProtocolV1DecodeStatus::Ok);
    assert(decoded.command.hasValue && decoded.command.value == -100);

    const std::string masterMute =
        R"({"version":1,"kind":"command","epoch":18446744073709551615,"sequence":9,"type":"SetMasterMute","flag":true})";
    assert(NativeUiProtocolV1Decoder::decode(masterMute, decoded) ==
        NativeUiProtocolV1DecodeStatus::Ok);
    assert(decoded.epoch == (std::numeric_limits<std::uint64_t>::max)());
    assert(decoded.command.hasFlag && decoded.command.flag);

    const auto accept = [&decoded](const std::string& json) {
        assert(NativeUiProtocolV1Decoder::decode(json, decoded) ==
            NativeUiProtocolV1DecodeStatus::Ok);
    };
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":10,"type":"SetTrackAuxSend","track":3,"index":2,"value":100})");
    assert(decoded.command.hasIndex && decoded.command.index == 2);
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":11,"type":"SetTrackEqBand","track":3,"index":1,"frequencyHz":1200,"q":0.8,"gainDb":-2.5,"sampleRate":96000})");
    assert(decoded.command.hasFrequencyHz && decoded.command.frequencyHz == 1200.0f);
    assert(decoded.command.hasQ && decoded.command.q == 0.8f);
    assert(decoded.command.hasGainDb && decoded.command.gainDb == -2.5f);
    assert(decoded.command.hasSampleRate && decoded.command.sampleRate == 96000.0f);
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":12,"type":"SetAudioInputPairLinked","flag":true})");
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":13,"type":"SetAudioInputPairStereoGain","value":96})");
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":14,"type":"SetAudioInputPairBalance","value":-35})");
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":15,"type":"SetAudioInputPairStereoMute","flag":true})");
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":16,"type":"SetAudioInputChannelGain","track":0,"value":90})");
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":17,"type":"SetAudioInputChannelPan","track":1,"value":-25})");
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":18,"type":"SetAudioInputChannelMute","track":0,"flag":true})");
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":19,"type":"SetAudioInputChannelSolo","track":1,"flag":true})");
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":20,"type":"SetAudioInputChannelAuxSend","track":1,"index":3,"value":75})");
    accept(R"({"version":1,"kind":"command","epoch":7,"sequence":21,"type":"SetAudioInputChannelEqBand","track":0,"index":5,"frequencyHz":8000,"q":0.8,"gainDb":1.5,"sampleRate":48000})");
    assert(decoded.command.hasSampleRate && decoded.command.sampleRate == 48000.0f);

    const auto reject = [&decoded](const std::string& json) {
        const auto oldEpoch = decoded.epoch;
        const auto oldSequence = decoded.command.sequence;
        const auto oldType = decoded.command.type;
        assert(NativeUiProtocolV1Decoder::decode(json, decoded) !=
            NativeUiProtocolV1DecodeStatus::Ok);
        assert(decoded.epoch == oldEpoch);
        assert(decoded.command.sequence == oldSequence);
        assert(decoded.command.type == oldType);
    };
    reject(R"({"version":2,"kind":"command","epoch":1,"sequence":1,"type":"SetMasterMute","flag":true})");
    reject(R"({"version":1,"kind":"hello","epoch":1,"sequence":1,"type":"SetMasterMute","flag":true})");
    reject(R"({"version":1,"kind":"command","epoch":0,"sequence":1,"type":"SetMasterMute","flag":true})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":0,"type":"SetMasterMute","flag":true})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetMasterMute","flag":true,"value":0})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetTrackFader","value":10})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetTrackFader","track":0,"value":10,"flag":false})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetTrackMute","track":0,"flag":1})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetTrackMute","track":-1,"flag":true})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetTrackMute","track":0.0,"flag":true})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetMasterFader","value":1.0})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetMasterFader","value":2147483648})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"Unknown","flag":true})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetMasterMute","flag":true,"epoch":2})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetMasterMute","flag":true,"extra":0})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetMasterMute","flag":true} trailing)");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetMasterMute","flag":true}{})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetMasterMute","flag":true.0})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetAudioInputPairLinked","flag":true,"track":0})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetAudioInputChannelAuxSend","track":0,"index":4.0,"value":50})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetAudioInputChannelEqBand","track":0,"index":1,"frequencyHz":1000,"q":1,"gainDb":0})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetAudioInputChannelEqBand","track":0,"index":1,"frequencyHz":1000,"q":1,"gainDb":0,"sampleRate":"48000"})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetTrackEqBand","track":0,"index":1,"frequencyHz":1000,"q":1,"gainDb":0,"sampleRate":48000,"flag":false})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetAudioInputChannelEqBand","track":0,"index":1,"frequencyHz":1e999,"q":1,"gainDb":0,"sampleRate":48000})");
    reject(R"({"version":1,"kind":"command","epoch":1,"sequence":1,"type":"SetAudioInputChannelEqBand","track":0,"index":1,"frequencyHz":1000,"q":1,"gainDb":0,"sampleRate":48000.0,"sampleRate":48000})");

    assert(NativeUiProtocolV1Decoder::decode("", decoded) ==
        NativeUiProtocolV1DecodeStatus::Empty);
    std::string oversized(NativeUiProtocolV1Decoder::kMaximumPayloadBytes + 1, ' ');
    assert(NativeUiProtocolV1Decoder::decode(oversized, decoded) ==
        NativeUiProtocolV1DecodeStatus::TooLarge);
    return 0;
}
