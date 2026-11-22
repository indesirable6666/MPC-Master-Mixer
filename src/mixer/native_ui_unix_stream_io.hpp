#pragma once

#include <cerrno>
#include <cstddef>
#include <sys/socket.h>

namespace mpcmixer {

// One nonblocking-safe POSIX stream operation. This deliberately does not
// create, bind, connect, accept, or own descriptors: lifecycle and socket-path
// policy remain outside this adapter. Call only from a control/transport
// thread, never from the audio callback.
class NativeUiUnixStreamIo {
public:
    enum class Status {
        Progress,
        WouldBlock,
        PeerClosed,
        Error
    };

    struct Result {
        Status status;
        std::size_t bytes;
        int errorNumber;
    };

    static Result receive(
        int descriptor, void* destination, std::size_t capacity) noexcept {
        if (capacity == 0)
            return {Status::Progress, 0, 0};
        if (destination == nullptr)
            return {Status::Error, 0, EINVAL};

        for (;;) {
            const ssize_t count = ::recv(descriptor, destination, capacity, 0);
            if (count > 0)
                return {Status::Progress, static_cast<std::size_t>(count), 0};
            if (count == 0)
                return {Status::PeerClosed, 0, 0};
            const int error = errno;
            if (error == EINTR)
                continue;
            if (error == EAGAIN || error == EWOULDBLOCK)
                return {Status::WouldBlock, 0, error};
            return {Status::Error, 0, error};
        }
    }

    static Result transmit(
        int descriptor, const void* source, std::size_t size) noexcept {
        if (size == 0)
            return {Status::Progress, 0, 0};
        if (source == nullptr)
            return {Status::Error, 0, EINVAL};

        for (;;) {
#ifdef MSG_NOSIGNAL
            const ssize_t count =
                ::send(descriptor, source, size, MSG_NOSIGNAL);
#else
            const ssize_t count = ::send(descriptor, source, size, 0);
#endif
            if (count > 0)
                return {Status::Progress, static_cast<std::size_t>(count), 0};
            if (count == 0)
                return {Status::PeerClosed, 0, 0};
            const int error = errno;
            if (error == EINTR)
                continue;
            if (error == EAGAIN || error == EWOULDBLOCK)
                return {Status::WouldBlock, 0, error};
            return {Status::Error, 0, error};
        }
    }
};

} // namespace mpcmixer
