#include "../src/mixer/native_ui_connection_epoch.hpp"

#include <cassert>
#include <cstdint>
#include <limits>

int main() {
    mpcmixer::NativeUiConnectionEpoch epochs;
    std::uint64_t first = 99;

    assert(epochs.activeEpoch() == 0);
    assert(!epochs.isCurrent(0));
    assert(epochs.beginConnection(first));
    assert(first == 1);
    assert(epochs.isCurrent(first));
    assert(epochs.activeEpoch() == first);

    std::uint64_t unchanged = 77;
    assert(!epochs.beginConnection(unchanged));
    assert(unchanged == 77);
    assert(!epochs.invalidate(0));
    assert(!epochs.invalidate(first + 1));
    assert(epochs.isCurrent(first));

    assert(epochs.invalidate(first));
    assert(!epochs.isCurrent(first));
    assert(epochs.activeEpoch() == 0);

    std::uint64_t second = 0;
    assert(epochs.beginConnection(second));
    assert(second == 2);
    assert(epochs.isCurrent(second));

    // A delayed close from the old connection must not invalidate the new one.
    assert(!epochs.invalidate(first));
    assert(epochs.isCurrent(second));
    assert(epochs.invalidate(second));

    // Never wrap to the reserved zero epoch.
    mpcmixer::NativeUiConnectionEpoch exhausted(
        std::numeric_limits<std::uint64_t>::max() - 1);
    std::uint64_t finalEpoch = 0;
    assert(exhausted.beginConnection(finalEpoch));
    assert(finalEpoch == std::numeric_limits<std::uint64_t>::max());
    assert(exhausted.invalidate(finalEpoch));
    std::uint64_t untouched = 123;
    assert(!exhausted.beginConnection(untouched));
    assert(untouched == 123);
    assert(exhausted.activeEpoch() == 0);
    assert(exhausted.lastIssuedEpoch() ==
           std::numeric_limits<std::uint64_t>::max());
}
