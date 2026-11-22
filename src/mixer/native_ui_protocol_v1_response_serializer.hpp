#pragma once
#include "mixer_ui_command_dispatcher.hpp"
#include "../../third_party/nlohmann/json.hpp"
#include <cstdint>
#include <string>
namespace mpcmixer {
enum class NativeUiV1ProtocolErrorCode : std::uint8_t { UnsupportedVersion, InvalidMessage, StaleEpoch, NotSynchronized, MessageTooLarge, ServerBusy };
class NativeUiProtocolV1ResponseSerializer {
public:
 static bool serverHello(std::uint64_t epoch,std::string& output) {
  if(!epoch)return false;
  nlohmann::json j=envelope("hello",epoch);j["status"]="ready";return commit(j,output);
 }
 static bool commandResult(std::uint64_t epoch,const MixerUiCommandResult& result,std::string& output) {
  if(!epoch||!result.sequence)return false;
  const char* token=commandStatus(result.status);if(!token)return false;
  nlohmann::json j=envelope("command_result",epoch);j["sequence"]=result.sequence;j["status"]=token;j["audio_ticket"]=result.audioTicket;return commit(j,output);
 }
 static bool audioAck(std::uint64_t epoch,std::uint64_t sequence,std::uint32_t audioTicket,std::string& output) {
  if(!epoch||!sequence||!audioTicket)return false;
  nlohmann::json j=envelope("audio_ack",epoch);j["sequence"]=sequence;j["audio_ticket"]=audioTicket;j["status"]="applied";return commit(j,output);
 }
 static bool protocolError(std::uint64_t epoch,NativeUiV1ProtocolErrorCode code,bool fatal,std::string& output) {
  if(!epoch)return false;
  const char* token=errorCode(code);if(!token)return false;
  nlohmann::json j=envelope("protocol_error",epoch);j["code"]=token;j["fatal"]=fatal;return commit(j,output);
 }
private:
 static nlohmann::json envelope(const char* kind,std::uint64_t epoch) {
  nlohmann::json j=nlohmann::json::object();j["version"]=1;j["kind"]=kind;j["epoch"]=epoch;return j;
 }
 static bool commit(const nlohmann::json& j,std::string& output) {
  try { const std::string candidate=j.dump(-1,' ',false,nlohmann::json::error_handler_t::strict);output=candidate;return true; }
  catch(...) { return false; }
 }
 static const char* commandStatus(MixerUiCommandStatus s) {
  switch(s){case MixerUiCommandStatus::Accepted:return "accepted";case MixerUiCommandStatus::Invalid:return "invalid";case MixerUiCommandStatus::OutOfOrder:return "out_of_order";case MixerUiCommandStatus::QueueFull:return "queue_full";case MixerUiCommandStatus::Unsupported:return "unsupported";}
  return nullptr;
 }
 static const char* errorCode(NativeUiV1ProtocolErrorCode c) {
  switch(c){case NativeUiV1ProtocolErrorCode::UnsupportedVersion:return "unsupported_version";case NativeUiV1ProtocolErrorCode::InvalidMessage:return "invalid_message";case NativeUiV1ProtocolErrorCode::StaleEpoch:return "stale_epoch";case NativeUiV1ProtocolErrorCode::NotSynchronized:return "not_synchronized";case NativeUiV1ProtocolErrorCode::MessageTooLarge:return "message_too_large";case NativeUiV1ProtocolErrorCode::ServerBusy:return "server_busy";}
  return nullptr;
 }
};
} // namespace mpcmixer
