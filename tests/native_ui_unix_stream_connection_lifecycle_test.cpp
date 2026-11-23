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
    std::uint8_t closeProbe = 0;
    const auto closed = NativeUiUnixStreamIo::receive(
        first.get(), &closeProbe, sizeof(closeProbe));
    assert(closed.status == NativeUiUnixStreamIo::Status::PeerClosed);
    // The transport owner explicitly drops the old-epoch frame before reuse.
    writer.cancelPending();
    first.reset();
    assert(!first.valid());
    assert(!writer.pending());

    // Force a partial nonblocking write, then cancel the remainder before
    // the writer is reused. A small send buffer and a much larger frame make
    // EAGAIN deterministic without relying on peer timing.
    int partialPair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, partialPair) == 0);
    NativeUiUnixSocketDescriptor partial(partialPair[0]);
    NativeUiUnixSocketDescriptor partialPeer(partialPair[1]);
    assert(partial.setNonBlocking());
    int sendBuffer = 1024;
    assert(::setsockopt(partial.get(), SOL_SOCKET, SO_SNDBUF,
                        &sendBuffer, sizeof(sendBuffer)) == 0);
    std::uint8_t largeStorage[60004]{};
    std::uint8_t largePayload[60000]{};
    NativeUiUnixStreamFrameWriter partialWriter(
        largeStorage, sizeof(largeStorage), sizeof(largePayload));
    assert(partialWriter.queue(largePayload, sizeof(largePayload)).status ==
           NativeUiUnixStreamFrameWriter::Status::Queued);
    const auto partialResult = partialWriter.flush(partial.get());
    assert(partialResult.status ==
           NativeUiUnixStreamFrameWriter::Status::WouldBlock);
    assert(partialResult.bytes > 0);
    assert(partialWriter.pending());
    assert(partialWriter.remainingBytes() > 0);
    partialWriter.cancelPending();
    assert(!partialWriter.pending());
    assert(partialWriter.remainingBytes() == 0);

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

    // A partially received payload from an old epoch must not consume bytes
    // from the next connection after reconnect.
    std::uint8_t epochTxStorage[128]{};
    std::uint8_t epochRxPayload[64]{};
    std::uint8_t epochRxScratch[9]{};
    NativeUiUnixStreamFrameWriter epochWriter(
        epochTxStorage, sizeof(epochTxStorage), 64);
    NativeUiUnixStreamFrameReaderPump epochReader(
        epochRxPayload, sizeof(epochRxPayload), sizeof(epochRxPayload),
        epochRxScratch, sizeof(epochRxScratch));
    NativeUiUnixStreamConnection connection(epochWriter, epochReader);

    int oldEpochPair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, oldEpochPair) == 0);
    std::uint64_t oldEpoch = 0;
    assert(connection.adopt(oldEpochPair[0], oldEpoch));
    std::uint8_t oldPartialFrame[6]{};
    assert(NativeUiFrameCodec::encodeLength(4, oldPartialFrame));
    oldPartialFrame[4] = 'o';
    oldPartialFrame[5] = 'l';
    const auto oldPartialSent = NativeUiUnixStreamIo::transmit(
        oldEpochPair[1], oldPartialFrame, sizeof(oldPartialFrame));
    assert(oldPartialSent.status == NativeUiUnixStreamIo::Status::Progress);
    assert(oldPartialSent.bytes == sizeof(oldPartialFrame));
    assert(epochReader.receiveNext(connection.descriptor()) ==
           NativeUiFrameCodec::Status::NeedMore);
    assert(!epochReader.hasFrame());
    assert(connection.invalidate(oldEpoch));
    ::close(oldEpochPair[1]);

    int newEpochPair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, newEpochPair) == 0);
    std::uint64_t newEpoch = 0;
    assert(connection.adopt(newEpochPair[0], newEpoch));
    assert(newEpoch != oldEpoch);
    std::uint8_t freshFrame[8]{};
    assert(NativeUiFrameCodec::encodeLength(4, freshFrame));
    std::memcpy(freshFrame + NativeUiFrameCodec::kHeaderBytes, "new!", 4);
    const auto freshSent = NativeUiUnixStreamIo::transmit(
        newEpochPair[1], freshFrame, sizeof(freshFrame));
    assert(freshSent.status == NativeUiUnixStreamIo::Status::Progress);
    assert(freshSent.bytes == sizeof(freshFrame));
    assert(epochReader.receiveNext(connection.descriptor()) ==
           NativeUiFrameCodec::Status::FrameReady);
    assert(epochReader.payloadSize() == 4);
    assert(std::memcmp(epochReader.payload(), "new!", 4) == 0);
    epochReader.acknowledgeFrame();
    assert(connection.invalidate(newEpoch));
    ::close(newEpochPair[1]);
    return 0;
}
