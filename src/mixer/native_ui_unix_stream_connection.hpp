#pragma once

#include "native_ui_session_coordinator.hpp"
#include "native_ui_unix_socket_descriptor.hpp"
#include "native_ui_unix_stream_frame_channel.hpp"

#include <cstdint>
#include <utility>

namespace mpcmixer {

// Control-thread-only owner for one already-created AF_UNIX stream descriptor.
// adopt() takes ownership once it accepts a nonnegative descriptor while
// disconnected, including if descriptor setup or session creation then fails.
// Rejected preconditions leave ownership with the caller. This class does not
// create, bind, connect, listen, or accept sockets.
class NativeUiUnixStreamConnection {
public:
    explicit NativeUiUnixStreamConnection(
        NativeUiUnixStreamFrameWriter& writer,
        NativeUiUnixStreamFrameReaderPump& reader) noexcept
        : writer_(writer), reader_(reader) {}

    NativeUiUnixStreamConnection(const NativeUiUnixStreamConnection&) = delete;
    NativeUiUnixStreamConnection& operator=(
        const NativeUiUnixStreamConnection&) = delete;

    bool adopt(int descriptor, std::uint64_t& epoch) noexcept {
        if (descriptor < 0 || descriptor_.valid())
            return false;

        NativeUiUnixSocketDescriptor candidate(descriptor);
        if (!candidate.setNonBlocking() || !candidate.setCloseOnExec())
            return false;

        std::uint64_t newEpoch = 0;
        if (!session_.beginConnection(newEpoch))
            return false;

        descriptor_ = std::move(candidate);
        epoch_ = newEpoch;
        epoch = newEpoch;
        return true;
    }

    // Drop queued bytes before protocol-level pending responses are cancelled.
    // This lets the protocol owner enforce teardown ordering explicitly.
    void cancelPendingOutput() noexcept { writer_.cancelPending(); }

    bool invalidate(std::uint64_t epoch) noexcept {
        if (!descriptor_.valid() || epoch == 0 || epoch != epoch_)
            return false;
        if (!session_.invalidate(epoch))
            return false;

        // Clear both transport directions before releasing the old descriptor,
        // so no buffered bytes or queued frame can cross into a later epoch.
        writer_.cancelPending();
        reader_.resetConnection();
        descriptor_.reset();
        epoch_ = 0;
        return true;
    }

    int descriptor() const noexcept { return descriptor_.get(); }
    bool connected() const noexcept { return descriptor_.valid(); }
    std::uint64_t epoch() const noexcept { return epoch_; }
    NativeUiSessionCoordinator& session() noexcept { return session_; }
    const NativeUiSessionCoordinator& session() const noexcept { return session_; }

private:
    NativeUiUnixSocketDescriptor descriptor_;
    NativeUiUnixStreamFrameWriter& writer_;
    NativeUiUnixStreamFrameReaderPump& reader_;
    NativeUiSessionCoordinator session_;
    std::uint64_t epoch_ = 0;
};

} // namespace mpcmixer
