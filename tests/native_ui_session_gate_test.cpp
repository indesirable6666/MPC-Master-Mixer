#include "../src/mixer/native_ui_session_gate.hpp"

#include <cassert>
#include <cstdint>

int main() {
    using Gate = mpcmixer::NativeUiSessionGate;

    Gate gate;
    assert(gate.state() == Gate::State::Disconnected);
    assert(gate.activeEpoch() == 0);
    assert(gate.lastSnapshotSequence() == 0);
    assert(!gate.acceptsCommands(0));
    assert(!gate.begin(0));

    assert(gate.begin(11));
    assert(gate.activeEpoch() == 11);
    assert(gate.state() == Gate::State::Synchronizing);
    assert(!gate.acceptsCommands(11));
    assert(!gate.begin(12));

    // Partial, stale-epoch, zero-sequence and out-of-order snapshots are ignored.
    assert(!gate.updateSnapshot(11, 1, false, true));
    assert(!gate.updateSnapshot(12, 1, true, true));
    assert(!gate.updateSnapshot(11, 0, true, true));
    assert(!gate.acceptsCommands(11));

    // A full snapshot can describe desired state that audio has not applied.
    assert(gate.updateSnapshot(11, 4, true, false));
    assert(gate.lastSnapshotSequence() == 4);
    assert(gate.state() == Gate::State::Synchronizing);
    assert(!gate.acceptsCommands(11));

    // Older and duplicate snapshots cannot regress or alter the current state.
    assert(!gate.updateSnapshot(11, 3, true, true));
    assert(!gate.updateSnapshot(11, 4, true, true));
    assert(gate.state() == Gate::State::Synchronizing);
    assert(!gate.acceptsCommands(11));

    // A newer caught-up snapshot unlocks the session.
    assert(gate.updateSnapshot(11, 5, true, true));
    assert(gate.state() == Gate::State::Ready);
    assert(gate.acceptsCommands(11));
    assert(!gate.acceptsCommands(12));

    // A delayed older snapshot cannot downgrade a ready session.
    assert(!gate.updateSnapshot(11, 4, true, false));
    assert(gate.acceptsCommands(11));
    assert(gate.lastSnapshotSequence() == 5);

    // A newer not-caught-up snapshot conservatively closes the gate again.
    assert(gate.updateSnapshot(11, 6, true, false));
    assert(gate.state() == Gate::State::Synchronizing);
    assert(!gate.acceptsCommands(11));
    assert(gate.updateSnapshot(11, 7, true, true));
    assert(gate.acceptsCommands(11));

    // A delayed disconnect from an obsolete epoch cannot close this session.
    assert(!gate.invalidate(10));
    assert(gate.acceptsCommands(11));

    assert(gate.invalidate(11));
    assert(gate.state() == Gate::State::Disconnected);
    assert(gate.activeEpoch() == 0);
    assert(gate.lastSnapshotSequence() == 0);
    assert(!gate.acceptsCommands(11));
    assert(!gate.updateSnapshot(11, 8, true, true));

    // Reconnection starts closed and has an independent snapshot sequence.
    assert(gate.begin(12));
    assert(!gate.updateSnapshot(11, 9, true, true));
    assert(!gate.acceptsCommands(12));
    assert(gate.updateSnapshot(12, 1, true, true));
    assert(gate.acceptsCommands(12));

    // Stale disconnects and zero epochs remain harmless after reconnect.
    assert(!gate.invalidate(11));
    assert(!gate.invalidate(0));
    assert(gate.acceptsCommands(12));
    assert(gate.invalidate(12));
    assert(gate.state() == Gate::State::Disconnected);
}
