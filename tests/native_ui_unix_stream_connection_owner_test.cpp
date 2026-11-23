#include "native_ui_unix_stream_connection.hpp"
#include "native_ui_frame_codec.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

using namespace mpcmixer;

int main() {
    std::uint8_t txStorage[128]{};
    NativeUiUnixStreamFrameWriter writer(txStorage, sizeof(txStorage), 64);
    std::uint8_t rxPayload[64]{};
    std::uint8_t rxScratch[64]{};
    NativeUiUnixStreamFrameReaderPump reader(
        rxPayload, sizeof(rxPayload), 64, rxScratch, sizeof(rxScratch));
    NativeUiUnixStreamConnection connection(writer, reader);

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

    // Leave an incomplete frame header buffered by the receive pump.
    const std::uint8_t partialHeader[2] = {0, 0};
    assert(::send(firstPair[1], partialHeader, sizeof(partialHeader), 0) ==
           static_cast<ssize_t>(sizeof(partialHeader)));
    assert(reader.receiveNext(connection.descriptor()) ==
           NativeUiFrameCodec::Status::NeedMore);

    assert(writer.queue("old", 4).status ==
           NativeUiUnixStreamFrameWriter::Status::Queued);
    assert(!connection.invalidate(firstEpoch + 1));
    assert(writer.pending());
    assert(connection.invalidate(firstEpoch));
    assert(!connection.connected());
    assert(connection.epoch() == 0);
    assert(!writer.pending());
    assert(!reader.hasFrame());
    ::close(firstPair[1]);

    // Epochs increase across reconnects; stale invalidation cannot tear down
    // the new descriptor.
    std::uint64_t secondEpoch = 0;
    int secondPair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, secondPair) == 0);
    assert(connection.adopt(secondPair[0], secondEpoch));
    assert(secondEpoch > firstEpoch);
    assert(!connection.invalidate(firstEpoch));

    // A fresh frame is decoded independently after reconnect; the old
    // partial header must not prefix or corrupt it.
    std::uint8_t frame[7]{};
    assert(NativeUiFrameCodec::encodeLength(3, frame));
    frame[4] = 'n';
    frame[5] = 'e';
    frame[6] = 'w';
    assert(::send(secondPair[1], frame, sizeof(frame), 0) ==
           static_cast<ssize_t>(sizeof(frame)));
    assert(reader.receiveNext(connection.descriptor()) ==
           NativeUiFrameCodec::Status::FrameReady);
    assert(reader.payloadSize() == 3);
    assert(std::memcmp(reader.payload(), "new", 3) == 0);
    reader.acknowledgeFrame();
    assert(connection.connected());
    assert(connection.epoch() == secondEpoch);
    assert(connection.invalidate(secondEpoch));
    ::close(secondPair[1]);
    return 0;
}
