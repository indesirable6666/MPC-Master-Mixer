#include "native_ui_unix_stream_connection.hpp"

#include <cassert>
#include <cstdint>
#include <sys/socket.h>
#include <unistd.h>

using namespace mpcmixer;

int main() {
    std::uint8_t txStorage[128]{};
    NativeUiUnixStreamFrameWriter writer(txStorage, sizeof(txStorage), 64);
    NativeUiUnixStreamConnection connection(writer);

    std::uint64_t firstEpoch = 0;
    int firstPair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, firstPair) == 0);
    assert(connection.adopt(firstPair[0], firstEpoch));
    assert(firstEpoch != 0);
    assert(connection.connected());
    assert(connection.epoch() == firstEpoch);
    assert(connection.session().activeEpoch() == firstEpoch);

    // A live connection cannot be silently replaced; ownership stays with the
    // caller when adopt refuses an already-connected owner.
    int rejectedPair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, rejectedPair) == 0);
    std::uint64_t rejectedEpoch = 99;
    assert(!connection.adopt(rejectedPair[0], rejectedEpoch));
    assert(rejectedEpoch == 99);
    assert(::fcntl(rejectedPair[0], F_GETFD) >= 0);
    ::close(rejectedPair[0]);
    ::close(rejectedPair[1]);

    assert(writer.queue("old", 4).status ==
           NativeUiUnixStreamFrameWriter::Status::Queued);
    assert(!connection.invalidate(firstEpoch + 1));
    assert(writer.pending());
    assert(connection.invalidate(firstEpoch));
    assert(!connection.connected());
    assert(connection.epoch() == 0);
    assert(!writer.pending());
    ::close(firstPair[1]);

    // Epochs increase across reconnects; stale invalidation cannot tear down
    // the new descriptor.
    std::uint64_t secondEpoch = 0;
    int secondPair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, secondPair) == 0);
    assert(connection.adopt(secondPair[0], secondEpoch));
    assert(secondEpoch > firstEpoch);
    assert(!connection.invalidate(firstEpoch));
    assert(connection.connected());
    assert(connection.epoch() == secondEpoch);
    assert(connection.invalidate(secondEpoch));
    ::close(secondPair[1]);
    return 0;
}
