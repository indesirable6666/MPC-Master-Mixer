#include "native_ui_protocol_v1_snapshot_request_tracker.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

using namespace mpcmixer;

int main() {
    NativeUiProtocolV1SnapshotRequestTracker tracker;
    assert(!tracker.begin(0));
    assert(tracker.begin(7));
    assert(!tracker.begin(8));
    assert(!tracker.accept(8, 1));
    assert(!tracker.accept(7, 0));
    assert(tracker.accept(7, 1));
    assert(tracker.lastRequest() == 1);
    assert(!tracker.accept(7, 1));
    assert(!tracker.accept(7, 0));
    assert(tracker.accept(7, 4));
    assert(!tracker.accept(7, 3));
    assert(tracker.lastRequest() == 4);
    assert(!tracker.invalidate(8));
    assert(tracker.invalidate(7));
    assert(tracker.activeEpoch() == 0);
    assert(tracker.lastRequest() == 0);
    assert(!tracker.accept(7, 5));
    assert(tracker.begin(8));
    assert(tracker.accept(8, 1));
    assert(tracker.accept(8, (std::numeric_limits<std::uint64_t>::max)()));
    assert(!tracker.accept(8, (std::numeric_limits<std::uint64_t>::max)()));
    assert(tracker.invalidate(8));
    return 0;
}
