#pragma once

#include <fcntl.h>
#include <unistd.h>

namespace mpcmixer {

// Move-only owner for an already-created POSIX descriptor. This class does
// not create or connect sockets. Construct/adopt and destroy only on a
// control/transport thread, never in an audio callback.
class NativeUiUnixSocketDescriptor {
public:
    NativeUiUnixSocketDescriptor() noexcept = default;
    explicit NativeUiUnixSocketDescriptor(int descriptor) noexcept
        : descriptor_(descriptor) {}

    ~NativeUiUnixSocketDescriptor() { reset(); }

    NativeUiUnixSocketDescriptor(const NativeUiUnixSocketDescriptor&) = delete;
    NativeUiUnixSocketDescriptor& operator=(const NativeUiUnixSocketDescriptor&) = delete;

    NativeUiUnixSocketDescriptor(NativeUiUnixSocketDescriptor&& other) noexcept
        : descriptor_(other.release()) {}

    NativeUiUnixSocketDescriptor& operator=(
        NativeUiUnixSocketDescriptor&& other) noexcept {
        if (this != &other)
            reset(other.release());
        return *this;
    }

    int get() const noexcept { return descriptor_; }
    bool valid() const noexcept { return descriptor_ >= 0; }

    // Transfers ownership to the caller without closing the descriptor.
    int release() noexcept {
        const int result = descriptor_;
        descriptor_ = -1;
        return result;
    }

    // Takes ownership of descriptor and closes the previous one. Passing the
    // currently owned descriptor is a no-op.
    void reset(int descriptor = -1) noexcept {
        if (descriptor == descriptor_)
            return;
        const int previous = descriptor_;
        descriptor_ = descriptor;
        if (previous >= 0)
            ::close(previous);
    }

    bool setNonBlocking(bool enabled = true) noexcept {
        if (!valid())
            return false;
        const int flags = ::fcntl(descriptor_, F_GETFL, 0);
        if (flags < 0)
            return false;
        const int updated = enabled ? (flags | O_NONBLOCK) : (flags & ~O_NONBLOCK);
        return updated == flags || ::fcntl(descriptor_, F_SETFL, updated) == 0;
    }

    bool setCloseOnExec(bool enabled = true) noexcept {
        if (!valid())
            return false;
        const int flags = ::fcntl(descriptor_, F_GETFD, 0);
        if (flags < 0)
            return false;
        const int updated = enabled ? (flags | FD_CLOEXEC) : (flags & ~FD_CLOEXEC);
        return updated == flags || ::fcntl(descriptor_, F_SETFD, updated) == 0;
    }

private:
    int descriptor_ = -1;
};

} // namespace mpcmixer
