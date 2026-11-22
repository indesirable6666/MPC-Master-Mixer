#include "../src/mixer/native_ui_command_adapter.hpp"
#include "../src/mixer/mixer_presentation_snapshot.hpp"
#include <cassert>
int main() {
 mpcmixer::AudioEngineCommandBridge bridge;
 mpcmixer::MixerController controller(bridge);
 mpcmixer::MixerUiCommandDispatcher dispatcher(controller);
 mpcmixer::NativeUiCommandMessage m;
 m.sequence=1; m.type="SetTrackFader"; m.hasTrack=true; m.track=5; m.hasValue=true; m.value=96;
 auto r=mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher);
 assert(r.accepted() && r.audioTicket!=0);
 m.sequence=2; m.type="SetTrackPan"; m.value=-35;
 assert(mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher).accepted());
 m.sequence=3; m.type="SetTrackMute"; m.hasFlag=true; m.flag=true;
 assert(mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher).accepted());
 m.sequence=4; m.type="SetTrackSolo";
 assert(mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher).accepted());
 m.sequence=5; m.type="SetMasterFader"; m.hasTrack=false; m.value=110;
 assert(mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher).accepted());
 m.sequence=6; m.type="SetMasterMute"; m.flag=true;
 assert(mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher).accepted());
 mpcmixer::MixerPresentationPublisher publisher(controller);
 const auto s=publisher.publish();
 assert(s.tracks[5].faderValue==96 && s.tracks[5].panPercent==-35);
 assert(s.tracks[5].mute && s.tracks[5].solo);
 assert(s.masterFaderValue==110 && s.masterMute);
 m.sequence=7; m.type="SetTrackFader"; m.hasValue=false;
 assert(mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher).status==mpcmixer::MixerUiCommandStatus::Invalid);
 assert(dispatcher.lastAcceptedSequence()==6);
 m.hasValue=true; m.value=128;
 assert(mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher).status==mpcmixer::MixerUiCommandStatus::Invalid);
 m.value=64; m.track=16;
 assert(mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher).status==mpcmixer::MixerUiCommandStatus::Invalid);
 m.type="SetMasterMute"; m.hasFlag=false;
 assert(mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher).status==mpcmixer::MixerUiCommandStatus::Invalid);
 m.type="SetInsertEnabled";
 assert(mpcmixer::NativeUiCommandAdapter::dispatch(m,dispatcher).status==mpcmixer::MixerUiCommandStatus::Invalid);
 assert(dispatcher.lastAcceptedSequence()==6);
}