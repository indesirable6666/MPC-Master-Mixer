#pragma once

#include <cstdint>
#include <limits>

namespace mpcmixer {

// Control-thread-only connection epoch source. Epoch zero means disconnected
// and is never issued. The counter is not reset when a connection closes.
class NativeUiConnectionEpoch {
public:
    // lastIssuedEpoch is useful when restoring a persisted transport counter
    // and permits deterministic exhaustion testing. Production may use zero.
    explicit NativeUiConnectionEpoch(
        std::uint64_t lastIssuedEpoch = 0) noexcept
        : lastIssuedEpoch_(lastIssuedEpoch) {}

    // Starts one connection. A current connection must first be invalidated.
    // Returns false without modifying output or state if already connected or
    // if incrementing would wrap the unsigned epoch counter.
    bool beginConnection(std::uint64_t& epoch) noexcept {
        if (activeEpoch_ != 0 ||
            lastIssuedEpoch_ == std::numeric_limits<std::uint64_t>::max())
            return false;

        ++lastIssuedEpoch_;
        activeEpoch_ = lastIssuedEpoch_;
        epoch = activeEpoch_;
        return true;
    }

    // Only the currently active epoch can invalidate the connection. A stale
    // disconnect notification cannot invalidate a newer connection.
    bool invalidate(std::uint64_t epoch) noexcept {
        if (epoch == 0 || epoch != activeEpoch_) return false;
        activeEpoch_ = 0;
        return true;
    }

    bool isCurrent(std::uint64_t epoch) const noexcept {
        return epoch != 0 && epoch == activeEpoch_;
    }

    std::uint64_t activeEpoch() const noexcept { return activeEpoch_; }
    std::uint64_t lastIssuedEpoch() const noexcept { return lastIssuedEpoch_; }

private:
    std::uint64_t lastIssuedEpoch_ = 0;
    std::uint64_t activeEpoch_ = 0;
};

} // namespace mpcmixer
