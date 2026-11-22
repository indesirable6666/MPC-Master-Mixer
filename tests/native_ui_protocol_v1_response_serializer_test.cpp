#include "native_ui_protocol_v1_response_serializer.hpp"
#include <cassert>
#include <cstdint>
#include <limits>
#include <string>
using namespace mpcmixer;
using Json=nlohmann::json;
int main(){
 std::string wire="unchanged";
 assert(NativeUiProtocolV1ResponseSerializer::serverHello(7,wire));
 auto j=Json::parse(wire);assert(j.size()==4&&j["version"]==1&&j["kind"]=="hello"&&j["epoch"]==7&&j["status"]=="ready");
 assert(NativeUiProtocolV1ResponseSerializer::commandResult(7,41,NativeUiV1CommandResultStatus::Accepted,19,wire));
 j=Json::parse(wire);assert(j.size()==6&&j["kind"]=="command_result"&&j["epoch"]==7&&j["sequence"]==41&&j["status"]=="accepted"&&j["audio_ticket"]==19);
 assert(NativeUiProtocolV1ResponseSerializer::commandResult(7,42,NativeUiV1CommandResultStatus::QueueFull,0,wire));
 j=Json::parse(wire);assert(j["status"]=="queue_full"&&j["audio_ticket"]==0);
 assert(NativeUiProtocolV1ResponseSerializer::audioAck(7,41,19,wire));
 j=Json::parse(wire);assert(j.size()==6&&j["kind"]=="audio_ack"&&j["epoch"]==7&&j["sequence"]==41&&j["audio_ticket"]==19&&j["status"]=="applied");
 assert(NativeUiProtocolV1ResponseSerializer::protocolError(7,NativeUiV1ProtocolErrorCode::InvalidMessage,true,wire));
 j=Json::parse(wire);assert(j.size()==5&&j["kind"]=="protocol_error"&&j["epoch"]==7&&j["code"]=="invalid_message"&&j["fatal"]==true);
 assert(NativeUiProtocolV1ResponseSerializer::protocolError(7,NativeUiV1ProtocolErrorCode::MessageTooLarge,false,wire));
 j=Json::parse(wire);assert(j["code"]=="message_too_large"&&j["fatal"]==false);
 const auto reject=[&wire](bool ok){const auto before=wire;assert(!ok);assert(wire==before);};
 reject(NativeUiProtocolV1ResponseSerializer::serverHello(0,wire));
 reject(NativeUiProtocolV1ResponseSerializer::commandResult(0,1,NativeUiV1CommandResultStatus::Accepted,0,wire));
 reject(NativeUiProtocolV1ResponseSerializer::commandResult(1,0,NativeUiV1CommandResultStatus::Accepted,0,wire));
 reject(NativeUiProtocolV1ResponseSerializer::audioAck(1,1,0,wire));
 reject(NativeUiProtocolV1ResponseSerializer::protocolError(0,NativeUiV1ProtocolErrorCode::ServerBusy,true,wire));
 const auto beforeInvalidEnum=wire;
 assert(!NativeUiProtocolV1ResponseSerializer::commandResult(1,1,static_cast<NativeUiV1CommandResultStatus>(255),0,wire));
 assert(wire==beforeInvalidEnum);
 assert(!NativeUiProtocolV1ResponseSerializer::protocolError(1,static_cast<NativeUiV1ProtocolErrorCode>(255),true,wire));
 assert(wire==beforeInvalidEnum);
 assert(NativeUiProtocolV1ResponseSerializer::commandResult(
  (std::numeric_limits<std::uint64_t>::max)(),(std::numeric_limits<std::uint64_t>::max)(),
  NativeUiV1CommandResultStatus::Unsupported,(std::numeric_limits<std::uint32_t>::max)(),wire));
 j=Json::parse(wire);assert(j["epoch"]==(std::numeric_limits<std::uint64_t>::max)());
 assert(j["sequence"]==(std::numeric_limits<std::uint64_t>::max)());
 assert(j["audio_ticket"]==(std::numeric_limits<std::uint32_t>::max)());
}
