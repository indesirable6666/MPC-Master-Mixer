#pragma once
#include "../../third_party/nlohmann/json.hpp"
#include <cstddef>
#include <cstdint>
#include <string>
namespace mpcmixer {
enum class NativeUiProtocolV1ClientMessageKind : std::uint8_t { Hello, SnapshotRequest };
struct NativeUiProtocolV1ClientMessage { NativeUiProtocolV1ClientMessageKind kind = NativeUiProtocolV1ClientMessageKind::Hello; std::uint64_t epoch = 0; std::uint64_t request = 0; std::string client; };
enum class NativeUiProtocolV1ClientDecodeStatus : std::uint8_t { Ok, Empty, TooLarge, InvalidJson, InvalidMessage };
class NativeUiProtocolV1ClientMessageDecoder {
public:
 static constexpr std::size_t kMaximumPayloadBytes=1024, kMaximumClientBytes=64;
 static NativeUiProtocolV1ClientDecodeStatus decode(const std::string& input, NativeUiProtocolV1ClientMessage& output) {
  if(input.empty()) return NativeUiProtocolV1ClientDecodeStatus::Empty;
  if(input.size()>kMaximumPayloadBytes) return NativeUiProtocolV1ClientDecodeStatus::TooLarge;
  Handler h; const bool parsed=nlohmann::json::sax_parse(input,&h,nlohmann::json::input_format_t::json,true,false);
  if(!parsed||!h.complete()) return NativeUiProtocolV1ClientDecodeStatus::InvalidJson;
  NativeUiProtocolV1ClientMessage candidate; if(!h.build(candidate)) return NativeUiProtocolV1ClientDecodeStatus::InvalidMessage;
  output=candidate; return NativeUiProtocolV1ClientDecodeStatus::Ok;
 }
private:
 class Handler: public nlohmann::json_sax<nlohmann::json> {
  enum class Field:std::uint8_t {None,Version,Kind,Epoch,Client,Request};
 public:
  bool null() override{return false;} bool boolean(bool) override{return false;}
  bool number_integer(number_integer_t v) override{return v<0?false:number_unsigned(static_cast<number_unsigned_t>(v));}
  bool number_unsigned(number_unsigned_t v) override {
   if(!ready()) return false;
   switch(field_){case Field::Version:if(v!=1)return false;version_=true;break;case Field::Epoch:epoch_=static_cast<std::uint64_t>(v);epochSeen_=true;break;case Field::Request:if(!v)return false;request_=static_cast<std::uint64_t>(v);requestSeen_=true;break;default:return false;}
   field_=Field::None;return true;
  }
  bool number_float(number_float_t,const string_t&) override{return false;}
  bool string(string_t& v) override {
   if(!ready())return false;
   if(field_==Field::Kind){if(v!="hello"&&v!="snapshot_request")return false;kind_=v;kindSeen_=true;}
   else if(field_==Field::Client){if(v.empty()||v.size()>kMaximumClientBytes)return false;client_=v;clientSeen_=true;}
   else return false; field_=Field::None;return true;
  }
  bool binary(binary_t&) override{return false;}
  bool start_object(std::size_t) override{if(started_||ended_||depth_)return false;started_=true;depth_=1;return true;}
  bool key(string_t& v) override{
   if(depth_!=1||ended_||field_!=Field::None)return false;std::uint8_t bit=0;
   if(v=="version"){field_=Field::Version;bit=1;}else if(v=="kind"){field_=Field::Kind;bit=2;}else if(v=="epoch"){field_=Field::Epoch;bit=4;}else if(v=="client"){field_=Field::Client;bit=8;}else if(v=="request"){field_=Field::Request;bit=16;}else return false;
   if(seen_&bit)return false;seen_=static_cast<std::uint8_t>(seen_|bit);return true;
  }
  bool end_object() override{if(depth_!=1||field_!=Field::None||ended_)return false;depth_=0;ended_=true;return true;}
  bool start_array(std::size_t) override{return false;} bool end_array() override{return false;}
  bool parse_error(std::size_t,const string_t&,const nlohmann::detail::exception&) override{return false;}
  bool complete()const noexcept{return started_&&ended_&&!depth_&&field_==Field::None;}
  bool build(NativeUiProtocolV1ClientMessage& out)const{
   if(!complete()||!version_||!kindSeen_||!epochSeen_)return false;
   if(kind_=="hello"){if(epoch_||!clientSeen_||requestSeen_||seen_!=15)return false;out.kind=NativeUiProtocolV1ClientMessageKind::Hello;out.client=client_;return true;}
   if(kind_=="snapshot_request"){if(!epoch_||!requestSeen_||clientSeen_||seen_!=23)return false;out.kind=NativeUiProtocolV1ClientMessageKind::SnapshotRequest;out.epoch=epoch_;out.request=request_;return true;}return false;
  }
 private: bool ready()const noexcept{return started_&&!ended_&&depth_==1&&field_!=Field::None;}
  Field field_=Field::None;std::uint8_t seen_=0;std::uint64_t epoch_=0,request_=0;std::string kind_,client_;std::size_t depth_=0;
  bool started_=false,ended_=false,version_=false,kindSeen_=false,epochSeen_=false,clientSeen_=false,requestSeen_=false;
 };
};
} // namespace mpcmixer
