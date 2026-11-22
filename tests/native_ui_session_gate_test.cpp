#include "../src/mixer/native_ui_session_gate.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using Gate = mpcmixer::NativeUiSessionGate;

    Gate gate;
    assert(gate.state() == Gate::State::Disconnected);
    assert(gate.activeEpoch() == 0);
    assert(!gate.acceptsCommands(0));
    assert(!gate.begin(0));

    assert(gate.begin(11));
    assert(gate.activeEpoch() == 11);
    assert(gate.state() == Gate::State::Synchronizing);
    assert(!gate.acceptsCommands(11));
    assert(!gate.begin(12));

    // Partial snapshots and snapshots from another connection are ignored.
    assert(!gate.updateSnapshot(11, false, true));
    assert(!gate.updateSnapshot(12, true, true));
    assert(!gate.acceptsCommands(11));

    // A full snapshot can still describe desired state that audio has not
    // applied yet. The UI must remain read-only until a later snapshot catches
    // up to its audio ticket watermark.
    assert(gate.updateSnapshot(11, true, false));
    assert(gate.state() == Gate::State::Synchronizing);
    assert(!gate.acceptsCommands(11));

    assert(gate.updateSnapshot(11, true, true));
    assert(gate.state() == Gate::State::Ready);
    assert(gate.acceptsCommands(11));
    assert(!gate.acceptsCommands(12));

    // A delayed disconnect from an obsolete epoch must not close this session.
    assert(!gate.invalidate(10));
    assert(gate.acceptsCommands(11));

    assert(gate.invalidate(11));
    assert(gate.state() == Gate::State::Disconnected);
    assert(gate.activeEpoch() == 0);
    assert(!gate.acceptsCommands(11));
    assert(!gate.updateSnapshot(11, true, true));

    // Reconnection starts closed again and requires its own complete,
    // caught-up snapshot; the old connection's snapshot cannot unlock it.
    assert(gate.begin(12));
    assert(!gate.updateSnapshot(11, true, true));
    assert(!gate.acceptsCommands(12));
    assert(gate.updateSnapshot(12, true, true));
    assert(gate.acceptsCommands(12));

    // Stale disconnects and zero epochs remain harmless after reconnect.
    assert(!gate.invalidate(11));
    assert(!gate.invalidate(0));
    assert(gate.acceptsCommands(12));
    assert(gate.invalidate(12));
    assert(gate.state() == Gate::State::Disconnected);
}
