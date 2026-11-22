#include "native_ui_protocol_v1_client_message_decoder.hpp"
#include <cassert>
#include <string>
using namespace mpcmixer;
int main(){
 NativeUiProtocolV1ClientMessage m;
 assert(NativeUiProtocolV1ClientMessageDecoder::decode(R"({"version":1,"kind":"hello","epoch":0,"client":"touch-mixer"})",m)==NativeUiProtocolV1ClientDecodeStatus::Ok);
 assert(m.kind==NativeUiProtocolV1ClientMessageKind::Hello&&m.epoch==0&&m.client=="touch-mixer");
 assert(NativeUiProtocolV1ClientMessageDecoder::decode(R"({"version":1,"kind":"snapshot_request","epoch":7,"request":1})",m)==NativeUiProtocolV1ClientDecodeStatus::Ok);
 assert(m.kind==NativeUiProtocolV1ClientMessageKind::SnapshotRequest&&m.epoch==7&&m.request==1);
 auto reject=[&m](const std::string&s){auto e=m.epoch,r=m.request;auto c=m.client;assert(NativeUiProtocolV1ClientMessageDecoder::decode(s,m)!=NativeUiProtocolV1ClientDecodeStatus::Ok);assert(m.epoch==e&&m.request==r&&m.client==c);};
 reject(R"({"version":2,"kind":"hello","epoch":0,"client":"x"})");
 reject(R"({"version":1,"kind":"hello","epoch":1,"client":"x"})");
 reject(R"({"version":1,"kind":"hello","epoch":0})");
 reject(R"({"version":1,"kind":"hello","epoch":0,"client":""})");
 reject(R"({"version":1,"kind":"hello","epoch":0,"client":"x","request":1})");
 reject(R"({"version":1,"kind":"snapshot_request","epoch":0,"request":1})");
 reject(R"({"version":1,"kind":"snapshot_request","epoch":7,"request":0})");
 reject(R"({"version":1,"kind":"snapshot_request","epoch":7,"request":1,"client":"x"})");
 reject(R"({"version":1,"kind":"snapshot_request","epoch":7})");
 reject(R"({"version":1,"kind":"unknown","epoch":1,"request":1})");
 reject(R"({"version":1,"kind":"hello","epoch":0,"client":"x","extra":1})");
 reject(R"({"version":1,"kind":"hello","epoch":0,"client":"x","client":"y"})");
 reject(R"({"version":1,"kind":"hello","epoch":0,"client":1})");
 reject(R"({"version":1,"kind":"hello","epoch":0,"client":"x"} trailing)");
 reject(R"({"version":1,"kind":"hello","epoch":0,"client":"x"}{})");
 reject(R"({"version":1,"kind":"snapshot_request","epoch":18446744073709551616,"request":1})");
 reject(std::string(R"({"version":1,"kind":"hello","epoch":0,"client":")")+std::string(65,'a')+R"("})");
 assert(NativeUiProtocolV1ClientMessageDecoder::decode("",m)==NativeUiProtocolV1ClientDecodeStatus::Empty);
 assert(NativeUiProtocolV1ClientMessageDecoder::decode(std::string(1025,' '),m)==NativeUiProtocolV1ClientDecodeStatus::TooLarge);
}
