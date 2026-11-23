#include "native_ui_unix_socket_descriptor.hpp"
#include "native_ui_unix_stream_frame_channel.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <sys/socket.h>
#include <unistd.h>

using namespace mpcmixer;

int main() {
    std::uint8_t txStorage[128]{};
    std::uint8_t rxPayload[64]{};
    std::uint8_t rxScratch[9]{};
    NativeUiUnixStreamFrameWriter writer(txStorage, sizeof(txStorage), 64);
    NativeUiUnixStreamFrameReaderPump reader(
        rxPayload, sizeof(rxPayload), sizeof(rxPayload),
        rxScratch, sizeof(rxScratch));

    // First connection: adopt an already-created descriptor and detect peer
    // shutdown without leaking the descriptor or retaining a queued frame.
    int firstPair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, firstPair) == 0);
    NativeUiUnixSocketDescriptor first(firstPair[0]);
    assert(first.setNonBlocking());
    assert(first.setCloseOnExec());
    assert(first.valid());
    assert(writer.queue("old", 4).status ==
           NativeUiUnixStreamFrameWriter::Status::Queued);
    ::close(firstPair[1]);
    const auto closed = writer.flush(first.get());
    assert(closed.status == NativeUiUnixStreamFrameWriter::Status::PeerClosed);
    first.reset();
    assert(!first.valid());
    assert(!writer.pending());

    // Reconnect using a fresh pair. No bytes or state from the old connection
    // may be observed on the new descriptor.
    int secondPair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, secondPair) == 0);
    NativeUiUnixSocketDescriptor second(secondPair[0]);
    NativeUiUnixSocketDescriptor peer(secondPair[1]);
    assert(second.setNonBlocking());
    assert(peer.setNonBlocking());
    assert(writer.queue("new-session", 12).status ==
           NativeUiUnixStreamFrameWriter::Status::Queued);
    assert(writer.flush(second.get()).status ==
           NativeUiUnixStreamFrameWriter::Status::Sent);
    assert(reader.receiveNext(peer.get()) == NativeUiFrameCodec::Status::FrameReady);
    assert(reader.payloadSize() == 12);
    assert(std::memcmp(reader.payload(), "new-session", 12) == 0);
    reader.acknowledgeFrame();

    const int released = second.release();
    assert(released >= 0);
    assert(!second.valid());
    ::close(released);
    return 0;
}
