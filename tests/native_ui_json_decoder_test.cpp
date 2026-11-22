#include "native_ui_json_decoder.hpp"

#include <cassert>
#include <cstdint>
#include <limits>
#include <string>

using namespace mpcmixer;

int main() {
    NativeUiCommandMessage message;
    assert(NativeUiJsonDecoder::decode(
        R"({"sequence":1,"type":"SetTrackFader","track":15,"value":127})",
        message) == NativeUiJsonDecodeStatus::Ok);
    assert(message.sequence == 1);
    assert(message.type == "SetTrackFader");
    assert(message.hasTrack && message.track == 15);
    assert(message.hasValue && message.value == 127);
    assert(!message.hasFlag);

    assert(NativeUiJsonDecoder::decode(
        R"({"sequence":2,"type":"SetTrackPan","track":0,"value":-100})",
        message) == NativeUiJsonDecodeStatus::Ok);
    assert(message.value == -100);

    assert(NativeUiJsonDecoder::decode(
        R"({"sequence":3,"type":"SetTrackMute","track":4,"flag":false})",
        message) == NativeUiJsonDecodeStatus::Ok);
    assert(message.hasFlag && !message.flag);
    assert(!message.hasValue);

    assert(NativeUiJsonDecoder::decode(
        R"({"sequence":4,"type":"SetTrackSolo","track":4,"flag":true})",
        message) == NativeUiJsonDecodeStatus::Ok);
    assert(message.hasFlag && message.flag);

    assert(NativeUiJsonDecoder::decode(
        R"({"sequence":5,"type":"SetMasterFader","value":0})",
        message) == NativeUiJsonDecodeStatus::Ok);
    assert(!message.hasTrack && message.hasValue && message.value == 0);

    assert(NativeUiJsonDecoder::decode(
        R"({"sequence":18446744073709551615,"type":"SetMasterMute","flag":true})",
        message) == NativeUiJsonDecodeStatus::Ok);
    assert(message.sequence == (std::numeric_limits<std::uint64_t>::max)());

    const auto reject = [&message](const std::string& json) {
        const std::uint64_t previousSequence = message.sequence;
        const NativeUiJsonDecodeStatus status =
            NativeUiJsonDecoder::decode(json, message);
        assert(status != NativeUiJsonDecodeStatus::Ok);
        assert(message.sequence == previousSequence);
    };

    assert(NativeUiJsonDecoder::decode("", message) ==
        NativeUiJsonDecodeStatus::Empty);
    reject(R"({"sequence":1,"type":"SetMasterMute","flag":true} trailing)");
    reject(R"({"sequence":1,"type":"SetMasterMute","flag":true}{})");
    reject(R"([])");
    reject(R"(null)");
    reject(R"(42)");
    reject(R"({"sequence":1,"type":"SetMasterMute","flag":true,"extra":0})");
    reject(R"({"sequence":1,"sequence":2,"type":"SetMasterMute","flag":true})");
    reject(R"({"sequence":1,"type":"SetMasterMute","flag":true,"flag":false})");
    reject(R"({"sequence":1,"type":"SetMasterMute","flag":true,"nested":{}})");
    reject(R"({"sequence":1,"type":"SetMasterMute","flag":true,"array":[]})");
    reject(R"({"sequence":1.0,"type":"SetMasterMute","flag":true})");
    reject(R"({"sequence":1e0,"type":"SetMasterMute","flag":true})");
    reject(R"({"sequence":-1,"type":"SetMasterMute","flag":true})");
    reject(R"({"sequence":"1","type":"SetMasterMute","flag":true})");
    reject(R"({"sequence":true,"type":"SetMasterMute","flag":true})");
    reject(R"({"sequence":0,"type":"SetMasterMute","flag":true})");
    reject(R"({"sequence":18446744073709551616,"type":"SetMasterMute","flag":true})");
    reject(R"({"sequence":1,"type":7,"flag":true})");
    reject(R"({"sequence":1,"type":" setmastermute ","flag":true})");
    reject(R"({"sequence":1,"type":"SetMasterMute","flag":0})");
    reject(R"({"sequence":1,"type":"SetMasterMute","flag":"true"})");
    reject(R"({"sequence":1,"type":"SetMasterMute","flag":null})");
    reject(R"({"sequence":1,"type":"SetMasterMute"})");
    reject(R"({"sequence":1,"type":"SetMasterMute","flag":true,"value":0})");
    assert(NativeUiJsonDecoder::decode(
        R"({"sequence":1,"type":"SetMasterFader","value":128})", message) ==
        NativeUiJsonDecodeStatus::Ok); // Range validation belongs to the typed adapter.
    assert(NativeUiJsonDecoder::decode(
        R"({"sequence":1,"type":"SetMasterFader","value":-1})", message) ==
        NativeUiJsonDecodeStatus::Ok); // Representable integer; adapter rejects the fader range.
    reject(R"({"sequence":1,"type":"SetMasterFader","value":1.5})");
    reject(R"({"sequence":1,"type":"SetMasterFader","value":2147483648})");
    reject(R"({"sequence":1,"type":"SetTrackFader","value":10})");
    reject(R"({"sequence":1,"type":"SetTrackFader","track":0,"value":10,"flag":false})");
    reject(R"({"sequence":1,"type":"SetTrackMute","track":0,"value":1,"flag":true})");
    reject(R"({"sequence":1,"type":"SetTrackMute","track":-1,"flag":true})");
    reject(R"({"sequence":1,"type":"SetTrackMute","track":0.0,"flag":true})");
    reject(R"({"sequence":1,"type":"SetTrackMute","track":0,"flag":1})");
    reject(R"({"sequence":1,"type":"Unknown","flag":true})");

    std::string oversized(NativeUiJsonDecoder::kMaximumFrameBytes + 1, ' ');
    assert(NativeUiJsonDecoder::decode(oversized, message) ==
        NativeUiJsonDecodeStatus::TooLarge);

    return 0;
}
