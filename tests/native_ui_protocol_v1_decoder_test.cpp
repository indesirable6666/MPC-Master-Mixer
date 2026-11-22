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

    const auto reject = [&decoded](const std::string& json) {
        const auto oldEpoch = decoded.epoch;
        const auto oldSequence = decoded.command.sequence;
        assert(NativeUiProtocolV1Decoder::decode(json, decoded) !=
            NativeUiProtocolV1DecodeStatus::Ok);
        assert(decoded.epoch == oldEpoch);
        assert(decoded.command.sequence == oldSequence);
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
    assert(NativeUiProtocolV1Decoder::decode("", decoded) ==
        NativeUiProtocolV1DecodeStatus::Empty);
    std::string oversized(NativeUiProtocolV1Decoder::kMaximumPayloadBytes + 1, ' ');
    assert(NativeUiProtocolV1Decoder::decode(oversized, decoded) ==
        NativeUiProtocolV1DecodeStatus::TooLarge);
    return 0;
}
