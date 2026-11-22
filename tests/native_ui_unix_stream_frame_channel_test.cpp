#include "native_ui_unix_stream_frame_channel.hpp"

#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

using namespace mpcmixer;

int main() {
    int sockets[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);
    const int flags = ::fcntl(sockets[0], F_GETFL, 0);
    assert(flags >= 0);
    assert(::fcntl(sockets[0], F_SETFL, flags | O_NONBLOCK) == 0);
    const int peerFlags = ::fcntl(sockets[1], F_GETFL, 0);
    assert(peerFlags >= 0);
    assert(::fcntl(sockets[1], F_SETFL, peerFlags | O_NONBLOCK) == 0);

    std::uint8_t txStorage[128]{};
    NativeUiUnixStreamFrameWriter writer(txStorage, sizeof(txStorage), 64);
    const char first[] = "first-frame";
    auto queued = writer.queue(first, sizeof(first));
    assert(queued.status == NativeUiUnixStreamFrameWriter::Status::Queued);
    assert(writer.pending());
    assert(writer.queue(first, sizeof(first)).status ==
           NativeUiUnixStreamFrameWriter::Status::Busy);
    auto sent = writer.flush(sockets[0]);
    assert(sent.status == NativeUiUnixStreamFrameWriter::Status::Sent);
    assert(!writer.pending());

    std::uint8_t payload[64]{};
    std::uint8_t scratch[7]{};
    NativeUiUnixStreamFrameReaderPump reader(
        payload, sizeof(payload), sizeof(payload), scratch, sizeof(scratch));
    auto status = reader.receiveNext(sockets[1]);
    assert(status == NativeUiFrameCodec::Status::FrameReady);
    assert(reader.payloadSize() == sizeof(first));
    assert(std::memcmp(reader.payload(), first, sizeof(first)) == 0);
    reader.acknowledgeFrame();

    // A concatenated pair remains buffered across frame acknowledgements.
    const char second[] = "second";
    std::uint8_t pair[2 * NativeUiFrameCodec::kHeaderBytes + sizeof(first) + sizeof(second)]{};
    std::uint8_t h1[4]{}, h2[4]{};
    assert(NativeUiFrameCodec::encodeLength(sizeof(first), h1));
    assert(NativeUiFrameCodec::encodeLength(sizeof(second), h2));
    std::size_t at = 0;
    std::memcpy(pair + at, h1, 4); at += 4;
    std::memcpy(pair + at, first, sizeof(first)); at += sizeof(first);
    std::memcpy(pair + at, h2, 4); at += 4;
    std::memcpy(pair + at, second, sizeof(second)); at += sizeof(second);
    auto rawSent = NativeUiUnixStreamIo::transmit(sockets[0], pair, at);
    assert(rawSent.status == NativeUiUnixStreamIo::Status::Progress);
    assert(rawSent.bytes == at);

    status = reader.receiveNext(sockets[1]);
    assert(status == NativeUiFrameCodec::Status::FrameReady);
    assert(reader.payloadSize() == sizeof(first));
    assert(std::memcmp(reader.payload(), first, sizeof(first)) == 0);
    reader.acknowledgeFrame();
    status = reader.receiveNext(sockets[1]);
    assert(status == NativeUiFrameCodec::Status::FrameReady);
    assert(reader.payloadSize() == sizeof(second));
    assert(std::memcmp(reader.payload(), second, sizeof(second)) == 0);
    reader.acknowledgeFrame();

    // Empty nonblocking socket reports NeedMore, not EOF.
    status = reader.receiveNext(sockets[1]);
    assert(status == NativeUiFrameCodec::Status::NeedMore);

    // Zero and over-limit frames are rejected before queueing.
    assert(writer.queue(first, 0).status ==
           NativeUiUnixStreamFrameWriter::Status::InvalidFrame);
    assert(writer.queue(first, 65).status ==
           NativeUiUnixStreamFrameWriter::Status::InvalidFrame);

    ::close(sockets[0]);
    status = reader.receiveNext(sockets[1]);
    assert(status == NativeUiFrameCodec::Status::CleanEof);
    ::close(sockets[1]);

    // Bad descriptors surface as transport errors mapped to invalid config.
    status = reader.receiveNext(-1);
    assert(status == NativeUiFrameCodec::Status::InvalidConfiguration);
    return 0;
}
