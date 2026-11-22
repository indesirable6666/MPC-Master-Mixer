#pragma once

#include <cstdint>

namespace mpcmixer {

// Control-thread-only guard for client snapshot request identifiers.
// Requests are scoped to one server-issued connection epoch and must increase
// strictly; rejected requests never advance the watermark.
class NativeUiProtocolV1SnapshotRequestTracker {
public:
    bool begin(std::uint64_t epoch) noexcept {
        if (epoch == 0 || activeEpoch_ != 0) return false;
        activeEpoch_ = epoch;
        lastRequest_ = 0;
        return true;
    }

    bool accept(std::uint64_t epoch, std::uint64_t request) noexcept {
        if (epoch == 0 || epoch != activeEpoch_ || request == 0 ||
            request <= lastRequest_) return false;
        lastRequest_ = request;
        return true;
    }

    bool invalidate(std::uint64_t epoch) noexcept {
        if (epoch == 0 || epoch != activeEpoch_) return false;
        activeEpoch_ = 0;
        lastRequest_ = 0;
        return true;
    }

    std::uint64_t activeEpoch() const noexcept { return activeEpoch_; }
    std::uint64_t lastRequest() const noexcept { return lastRequest_; }

private:
    std::uint64_t activeEpoch_ = 0;
    std::uint64_t lastRequest_ = 0;
};

} // namespace mpcmixer
