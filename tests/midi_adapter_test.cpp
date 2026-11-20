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

    std::cout << "MIDI adapter tests passed (no ALSA/device access).\n";
    return 0;
}
