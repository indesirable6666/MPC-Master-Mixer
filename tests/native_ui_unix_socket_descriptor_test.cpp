#include "native_ui_unix_socket_descriptor.hpp"

#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <sys/socket.h>
#include <unistd.h>
#include <utility>

using mpcmixer::NativeUiUnixSocketDescriptor;

int main() {
    int sockets[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, sockets) == 0);

    NativeUiUnixSocketDescriptor first(sockets[0]);
    NativeUiUnixSocketDescriptor second(sockets[1]);
    assert(first.valid() && second.valid());
    assert(first.setNonBlocking());
    assert(first.setCloseOnExec());
    const int statusFlags = ::fcntl(first.get(), F_GETFL, 0);
    const int descriptorFlags = ::fcntl(first.get(), F_GETFD, 0);
    assert(statusFlags >= 0 && (statusFlags & O_NONBLOCK) != 0);
    assert(descriptorFlags >= 0 && (descriptorFlags & FD_CLOEXEC) != 0);

    NativeUiUnixSocketDescriptor moved(std::move(first));
    assert(!first.valid());
    assert(moved.valid());
    assert(::fcntl(sockets[0], F_GETFD, 0) >= 0);

    NativeUiUnixSocketDescriptor assigned;
    assigned = std::move(moved);
    assert(!moved.valid());
    assert(assigned.get() == sockets[0]);

    const int released = assigned.release();
    assert(released == sockets[0]);
    assert(!assigned.valid());
    assert(::fcntl(released, F_GETFD, 0) >= 0);
    ::close(released);
    assert(::fcntl(released, F_GETFD, 0) == -1 && errno == EBADF);

    // reset closes an owned descriptor; reset to the same descriptor is safe.
    const int replacement = ::dup(sockets[1]);
    assert(replacement >= 0);
    assigned.reset(replacement);
    assert(assigned.get() == replacement);
    assigned.reset(replacement);
    assert(::fcntl(replacement, F_GETFD, 0) >= 0);
    assigned.reset();
    assert(!assigned.valid());
    assert(::fcntl(replacement, F_GETFD, 0) == -1 && errno == EBADF);

    // Moving an empty owner is safe.
    NativeUiUnixSocketDescriptor empty;
    NativeUiUnixSocketDescriptor emptyMoved(std::move(empty));
    assert(!empty.valid() && !emptyMoved.valid());

    second.reset();
    assert(::fcntl(sockets[1], F_GETFD, 0) == -1 && errno == EBADF);
    return 0;
}
