#include "midi_adapter.hpp"

#include <cassert>
#include <limits>
#include <iostream>

using namespace mpcmixer;

class RejectingMidiTransport final : public MidiTransport {
public:
    bool send(const MidiMessage&) override {
        ++attempts;
        return false;
    }
    unsigned attempts = 0;
};

int main() {
    SimulatedMidiTransport transport;
    VolumeMidiAdapter adapter(transport);

    // No guessed mapping: volume changes are rejected and emit nothing.
    assert(!adapter.hasVerifiedMapping());
    assert(!adapter.setVolume(0.5f));
    assert(transport.messages().empty());

    // Unverified or out-of-range mappings must remain disabled.
    assert(!adapter.configure(CcMapping{0, 7, false}));
    assert(!adapter.configure(CcMapping{16, 7, true}));
    assert(!adapter.configure(CcMapping{0, 128, true}));
    assert(transport.messages().empty());

    // A caller must explicitly provide a verified mapping.
    assert(adapter.configure(CcMapping{2, 11, true}));
    assert(adapter.hasVerifiedMapping());

    assert(adapter.setVolume(0.0f));
    assert(adapter.setVolume(0.5f));
    assert(adapter.setVolume(1.0f));
    assert(!adapter.setVolume(-0.01f));
    assert(!adapter.setVolume(1.01f));
    assert(!adapter.setVolume(std::numeric_limits<float>::quiet_NaN()));
    assert(!adapter.setVolume(std::numeric_limits<float>::infinity()));

    const auto& messages = transport.messages();
    assert(messages.size() == 3);
    assert(messages[0].status == 0xB2 && messages[0].data1 == 11 && messages[0].data2 == 0);
    assert(messages[1].status == 0xB2 && messages[1].data1 == 11 && messages[1].data2 == 64);
    assert(messages[2].status == 0xB2 && messages[2].data1 == 11 && messages[2].data2 == 127);

    adapter.clearMapping();
    assert(!adapter.hasVerifiedMapping());
    assert(!adapter.setVolume(0.75f));
    assert(transport.messages().size() == 3);

    // A failed reconfiguration clears the previous mapping: no stale CC is sent.
    assert(adapter.configure(CcMapping{1, 10, true}));
    assert(!adapter.configure(CcMapping{16, 10, true}));
    assert(!adapter.hasVerifiedMapping());
    assert(!adapter.setVolume(0.25f));
    assert(transport.messages().size() == 3);

    // Transport errors are propagated to the caller.
    RejectingMidiTransport rejectingTransport;
    VolumeMidiAdapter rejectingAdapter(rejectingTransport);
    assert(rejectingAdapter.configure(CcMapping{0, 7, true}));
    assert(!rejectingAdapter.setVolume(0.5f));
    assert(rejectingTransport.attempts == 1);

    // Each mixer track has an independent verified mapping.
    SimulatedMidiTransport trackTransport;
    TrackVolumeMidiAdapter tracks(trackTransport);
    assert(!tracks.hasVerifiedMapping(0));
    assert(!tracks.setTrackVolume(0, 0.5f));
    assert(!tracks.hasVerifiedMapping(16));
    assert(!tracks.setTrackVolume(16, 0.5f));
    assert(!tracks.configureTrack(16, CcMapping{0, 1, true}));
    assert(tracks.configureTrack(0, CcMapping{0, 20, true}));
    assert(tracks.configureTrack(1, CcMapping{3, 21, true}));
    assert(tracks.setTrackVolume(0, 0.5f));
    assert(tracks.setTrackVolume(1, 1.0f));
    assert(!tracks.setTrackVolume(2, 0.25f));
    assert(!tracks.setTrackVolume(0, std::numeric_limits<float>::quiet_NaN()));
    const auto& trackMessages = trackTransport.messages();
    assert(trackMessages.size() == 2);
    assert(trackMessages[0].status == 0xB0 && trackMessages[0].data1 == 20 &&
           trackMessages[0].data2 == 64);
    assert(trackMessages[1].status == 0xB3 && trackMessages[1].data1 == 21 &&
           trackMessages[1].data2 == 127);

    // Clearing or invalidating one track never alters another track's mapping.
    tracks.clearTrack(0);
    assert(!tracks.hasVerifiedMapping(0));
    assert(tracks.hasVerifiedMapping(1));
    assert(!tracks.configureTrack(1, CcMapping{16, 21, true}));
    assert(!tracks.hasVerifiedMapping(1));
    assert(!tracks.setTrackVolume(1, 0.5f));
    assert(tracks.configureTrack(1, CcMapping{3, 21, true}));
    tracks.clearAll();
    assert(!tracks.hasVerifiedMapping(1));
    assert(trackTransport.messages().size() == 2);

    // Pan is independently mapped per track and remains disabled by default.
    SimulatedMidiTransport panTransport;
    TrackPanMidiAdapter pans(panTransport);
    assert(!pans.hasVerifiedMapping(0));
    assert(!pans.setTrackPan(0, 0.0f));
    assert(!pans.configureTrack(16, CcMapping{0, 10, true}));
    assert(pans.configureTrack(0, CcMapping{1, 10, true}));
    assert(pans.configureTrack(1, CcMapping{4, 11, true}));
    assert(pans.setTrackPan(0, -1.0f));
    assert(pans.setTrackPan(0, 0.0f));
    assert(pans.setTrackPan(0, 1.0f));
    assert(pans.setTrackPan(1, 0.0f));
    assert(!pans.setTrackPan(0, -1.01f));
    assert(!pans.setTrackPan(0, 1.01f));
    assert(!pans.setTrackPan(0, std::numeric_limits<float>::quiet_NaN()));
    const auto& panMessages = panTransport.messages();
    assert(panMessages.size() == 4);
    assert(panMessages[0].status == 0xB1 && panMessages[0].data1 == 10 &&
           panMessages[0].data2 == 0);
    assert(panMessages[1].status == 0xB1 && panMessages[1].data1 == 10 &&
           panMessages[1].data2 == 64);
    assert(panMessages[2].status == 0xB1 && panMessages[2].data1 == 10 &&
           panMessages[2].data2 == 127);
    assert(panMessages[3].status == 0xB4 && panMessages[3].data1 == 11 &&
           panMessages[3].data2 == 64);
    pans.clearTrack(0);
    assert(!pans.hasVerifiedMapping(0));
    assert(pans.hasVerifiedMapping(1));
    pans.clearAll();
    assert(!pans.hasVerifiedMapping(1));
    assert(panTransport.messages().size() == 4);

    // Each track/Aux pair has an independent, explicitly verified mapping.
    SimulatedMidiTransport auxTransport;
    TrackAuxMidiAdapter auxSends(auxTransport);
    assert(!auxSends.hasVerifiedMapping(0, 0));
    assert(!auxSends.setSendLevel(0, 0, 0.5f));
    assert(!auxSends.configureSend(16, 0, CcMapping{0, 30, true}));
    assert(!auxSends.configureSend(0, 4, CcMapping{0, 30, true}));
    assert(auxSends.configureSend(0, 0, CcMapping{2, 30, true}));
    assert(auxSends.configureSend(0, 1, CcMapping{2, 31, true}));
    assert(auxSends.configureSend(1, 0, CcMapping{5, 40, true}));
    assert(auxSends.setSendLevel(0, 0, 0.0f));
    assert(auxSends.setSendLevel(0, 1, 0.5f));
    assert(auxSends.setSendLevel(1, 0, 1.0f));
    assert(!auxSends.setSendLevel(0, 2, 0.5f));
    assert(!auxSends.setSendLevel(0, 0, -0.01f));
    assert(!auxSends.setSendLevel(0, 0, 1.01f));
    assert(!auxSends.setSendLevel(0, 0,
                                  std::numeric_limits<float>::infinity()));
    const auto& auxMessages = auxTransport.messages();
    assert(auxMessages.size() == 3);
    assert(auxMessages[0].status == 0xB2 && auxMessages[0].data1 == 30 &&
           auxMessages[0].data2 == 0);
    assert(auxMessages[1].status == 0xB2 && auxMessages[1].data1 == 31 &&
           auxMessages[1].data2 == 64);
    assert(auxMessages[2].status == 0xB5 && auxMessages[2].data1 == 40 &&
           auxMessages[2].data2 == 127);

    // Invalid reconfiguration and clearing affect only the addressed slots.
    assert(!auxSends.configureSend(0, 0, CcMapping{16, 30, true}));
    assert(!auxSends.hasVerifiedMapping(0, 0));
    assert(auxSends.hasVerifiedMapping(0, 1));
    assert(auxSends.hasVerifiedMapping(1, 0));
    assert(!auxSends.setSendLevel(0, 0, 0.25f));
    auxSends.clearSend(0, 1);
    assert(!auxSends.hasVerifiedMapping(0, 1));
    assert(auxSends.hasVerifiedMapping(1, 0));
    auxSends.clearTrack(1);
    assert(!auxSends.hasVerifiedMapping(1, 0));
    assert(auxTransport.messages().size() == 3);
    assert(auxSends.configureSend(15, 3, CcMapping{15, 127, true}));
    assert(auxSends.hasVerifiedMapping(15, 3));
    auxSends.clearAll();
    assert(!auxSends.hasVerifiedMapping(15, 3));
    assert(!auxSends.setSendLevel(15, 3, 0.5f));
    assert(auxTransport.messages().size() == 3);


    // Mute and Solo use separate, opt-in mappings for each track.
    SimulatedMidiTransport switchTransport;
    TrackMuteSoloMidiAdapter switches(switchTransport);
    assert(!switches.hasMuteMapping(0));
    assert(!switches.hasSoloMapping(0));
    assert(!switches.setMuted(0, true));
    assert(!switches.setSolo(0, true));
    assert(!switches.configureMute(16, CcMapping{0, 50, true}));
    assert(!switches.configureSolo(0, CcMapping{0, 51, false}));
    assert(switches.configureMute(0, CcMapping{1, 50, true}));
    assert(switches.configureSolo(0, CcMapping{1, 51, true}));
    assert(switches.configureMute(1, CcMapping{3, 60, true}));
    assert(switches.setMuted(0, true));
    assert(switches.setMuted(0, false));
    assert(switches.setSolo(0, true));
    assert(switches.setSolo(0, false));
    assert(switches.setMuted(1, true));
    assert(!switches.setSolo(1, true));
    const auto& switchMessages = switchTransport.messages();
    assert(switchMessages.size() == 5);
    assert(switchMessages[0].status == 0xB1 && switchMessages[0].data1 == 50 &&
           switchMessages[0].data2 == 127);
    assert(switchMessages[1].status == 0xB1 && switchMessages[1].data1 == 50 &&
           switchMessages[1].data2 == 0);
    assert(switchMessages[2].status == 0xB1 && switchMessages[2].data1 == 51 &&
           switchMessages[2].data2 == 127);
    assert(switchMessages[3].status == 0xB1 && switchMessages[3].data1 == 51 &&
           switchMessages[3].data2 == 0);
    assert(switchMessages[4].status == 0xB3 && switchMessages[4].data1 == 60 &&
           switchMessages[4].data2 == 127);

    // Invalid reconfiguration clears only the addressed switch mapping.
    assert(!switches.configureMute(0, CcMapping{16, 50, true}));
    assert(!switches.hasMuteMapping(0));
    assert(switches.hasSoloMapping(0));
    assert(switches.hasMuteMapping(1));
    assert(!switches.setMuted(0, true));
    switches.clearSolo(0);
    assert(!switches.hasSoloMapping(0));
    assert(switches.hasMuteMapping(1));
    switches.clearTrack(1);
    assert(!switches.hasMuteMapping(1));
    assert(switches.configureMute(15, CcMapping{15, 127, true}));
    assert(switches.configureSolo(15, CcMapping{15, 126, true}));
    switches.clearAll();
    assert(!switches.hasMuteMapping(15));
    assert(!switches.hasSoloMapping(15));
    assert(!switches.setMuted(15, false));
    assert(switchTransport.messages().size() == 5);

    // A transport failure is propagated for switch messages as well.
    RejectingMidiTransport rejectingSwitchTransport;
    TrackMuteSoloMidiAdapter rejectingSwitches(rejectingSwitchTransport);
    assert(rejectingSwitches.configureMute(0, CcMapping{0, 70, true}));
    assert(!rejectingSwitches.setMuted(0, true));
    assert(rejectingSwitchTransport.attempts == 1);


    // Master bus has independent, opt-in Volume and Mute mappings.
    SimulatedMidiTransport masterTransport;
    MasterMidiAdapter master(masterTransport);
    assert(!master.hasVolumeMapping());
    assert(!master.hasMuteMapping());
    assert(!master.setVolume(0.5f));
    assert(!master.setMuted(true));
    assert(!master.configureVolume(CcMapping{0, 80, false}));
    assert(!master.configureMute(CcMapping{16, 81, true}));
    assert(masterTransport.messages().empty());
    assert(master.configureVolume(CcMapping{2, 80, true}));
    assert(master.configureMute(CcMapping{2, 81, true}));
    assert(master.setVolume(0.0f));
    assert(master.setVolume(0.5f));
    assert(master.setVolume(1.0f));
    assert(!master.setVolume(-0.1f));
    assert(!master.setVolume(std::numeric_limits<float>::quiet_NaN()));
    assert(master.setMuted(true));
    assert(master.setMuted(false));
    const auto& masterMessages = masterTransport.messages();
    assert(masterMessages.size() == 5);
    assert(masterMessages[0].status == 0xB2 && masterMessages[0].data1 == 80 &&
           masterMessages[0].data2 == 0);
    assert(masterMessages[1].status == 0xB2 && masterMessages[1].data1 == 80 &&
           masterMessages[1].data2 == 64);
    assert(masterMessages[2].status == 0xB2 && masterMessages[2].data1 == 80 &&
           masterMessages[2].data2 == 127);
    assert(masterMessages[3].status == 0xB2 && masterMessages[3].data1 == 81 &&
           masterMessages[3].data2 == 127);
    assert(masterMessages[4].status == 0xB2 && masterMessages[4].data1 == 81 &&
           masterMessages[4].data2 == 0);

    // Clearing or invalidating one Master control leaves the other independent.
    assert(!master.configureVolume(CcMapping{16, 80, true}));
    assert(!master.hasVolumeMapping());
    assert(master.hasMuteMapping());
    assert(!master.setVolume(0.75f));
    assert(master.setMuted(true));
    master.clearMute();
    assert(!master.hasMuteMapping());
    assert(master.configureVolume(CcMapping{1, 82, true}));
    master.clearAll();
    assert(!master.hasVolumeMapping());
    assert(!master.hasMuteMapping());
    assert(!master.setVolume(0.5f));
    assert(masterTransport.messages().size() == 6);

    // Master transport errors are propagated.
    RejectingMidiTransport rejectingMasterTransport;
    MasterMidiAdapter rejectingMaster(rejectingMasterTransport);
    assert(rejectingMaster.configureVolume(CcMapping{0, 90, true}));
    assert(!rejectingMaster.setVolume(0.5f));

    std::cout << "MIDI adapter tests passed (no ALSA/device access).\n";
    return 0;
}
