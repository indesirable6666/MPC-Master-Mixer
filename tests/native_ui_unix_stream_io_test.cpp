#include "native_ui_unix_stream_io.hpp"

#include <cassert>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>

using mpcmixer::NativeUiUnixStreamIo;

int main() {
    int sockets[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    const char message[] = "mixer-frame";
    auto sent = NativeUiUnixStreamIo::transmit(
        sockets[0], message, sizeof(message));
    assert(sent.status == NativeUiUnixStreamIo::Status::Progress);
    assert(sent.bytes == sizeof(message));

    char received[32] = {};
    auto got = NativeUiUnixStreamIo::receive(
        sockets[1], received, sizeof(received));
    assert(got.status == NativeUiUnixStreamIo::Status::Progress);
    assert(got.bytes == sizeof(message));
    assert(std::memcmp(received, message, sizeof(message)) == 0);

    // Zero-length operations are harmless and do not issue syscalls.
    auto emptyWrite = NativeUiUnixStreamIo::transmit(sockets[0], nullptr, 0);
    auto emptyRead = NativeUiUnixStreamIo::receive(sockets[1], nullptr, 0);
    assert(emptyWrite.status == NativeUiUnixStreamIo::Status::Progress);
    assert(emptyRead.status == NativeUiUnixStreamIo::Status::Progress);
    assert(emptyWrite.bytes == 0 && emptyRead.bytes == 0);

    // Nonblocking empty reads report backpressure, not EOF or a hard error.
    const int flags = ::fcntl(sockets[1], F_GETFL, 0);
    assert(flags >= 0);
    assert(::fcntl(sockets[1], F_SETFL, flags | O_NONBLOCK) == 0);
    got = NativeUiUnixStreamIo::receive(sockets[1], received, sizeof(received));
    assert(got.status == NativeUiUnixStreamIo::Status::WouldBlock);
    assert(got.bytes == 0);
    assert(got.errorNumber == EAGAIN || got.errorNumber == EWOULDBLOCK);

    auto invalidBuffer = NativeUiUnixStreamIo::transmit(sockets[0], nullptr, 1);
    assert(invalidBuffer.status == NativeUiUnixStreamIo::Status::Error);
    assert(invalidBuffer.errorNumber == EINVAL);

    ::close(sockets[0]);
    got = NativeUiUnixStreamIo::receive(sockets[1], received, sizeof(received));
    assert(got.status == NativeUiUnixStreamIo::Status::PeerClosed);
    ::close(sockets[1]);

    // A closed descriptor is reported without terminating the process.
    got = NativeUiUnixStreamIo::receive(-1, received, sizeof(received));
    assert(got.status == NativeUiUnixStreamIo::Status::Error);
    assert(got.errorNumber == EBADF);
    return 0;
}
