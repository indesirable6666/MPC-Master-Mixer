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

    std::cout << "MIDI adapter tests passed (no ALSA/device access).\n";
    return 0;
}
