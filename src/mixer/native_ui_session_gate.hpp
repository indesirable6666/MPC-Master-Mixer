#pragma once

#include <cstdint>

namespace mpcmixer {

// Control-thread-only readiness gate for one native UI connection. The epoch
// source remains NativeUiConnectionEpoch; this class only gates command flow.
class NativeUiSessionGate {
public:
    enum class State {
        Disconnected,
        Synchronizing,
        Ready
    };

    // Starts a session for an already-issued, nonzero connection epoch.
    // The caller must invalidate the previous epoch before beginning another.
    bool begin(std::uint64_t epoch) noexcept {
        if (epoch == 0 || state_ != State::Disconnected) return false;
        activeEpoch_ = epoch;
        lastSnapshotSequence_ = 0;
        state_ = State::Synchronizing;
        return true;
    }

    // Call only after receiving a complete native snapshot for this epoch.
    // Snapshot sequences must be nonzero and strictly increasing. This rejects
    // delayed/duplicate snapshots so they cannot regress readiness.
    // A snapshot whose audio watermark is not yet applied keeps commands
    // disabled; a later caught-up snapshot transitions the session to Ready.
    bool updateSnapshot(
        std::uint64_t epoch,
        std::uint64_t snapshotSequence,
        bool fullSnapshotReceived,
        bool audioStateCaughtUp) noexcept {
        if (epoch == 0 || epoch != activeEpoch_ ||
            state_ == State::Disconnected || !fullSnapshotReceived ||
            snapshotSequence == 0 || snapshotSequence <= lastSnapshotSequence_)
            return false;

        lastSnapshotSequence_ = snapshotSequence;
        state_ = audioStateCaughtUp ? State::Ready : State::Synchronizing;
        return true;
    }

    // A delayed close from an older connection cannot close the active one.
    bool invalidate(std::uint64_t epoch) noexcept {
        if (epoch == 0 || epoch != activeEpoch_ ||
            state_ == State::Disconnected)
            return false;

        activeEpoch_ = 0;
        lastSnapshotSequence_ = 0;
        state_ = State::Disconnected;
        return true;
    }

    bool isReady(std::uint64_t epoch) const noexcept {
        return epoch != 0 && epoch == activeEpoch_ &&
            state_ == State::Ready;
    }

    bool acceptsCommands(std::uint64_t epoch) const noexcept {
        return isReady(epoch);
    }

    State state() const noexcept { return state_; }
    std::uint64_t activeEpoch() const noexcept { return activeEpoch_; }
    std::uint64_t lastSnapshotSequence() const noexcept {
        return lastSnapshotSequence_;
    }

private:
    std::uint64_t activeEpoch_ = 0;
    std::uint64_t lastSnapshotSequence_ = 0;
    State state_ = State::Disconnected;
};

} // namespace mpcmixer
