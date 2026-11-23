#pragma once

#include "native_ui_protocol_v1_response_serializer.hpp"
#include "native_ui_session_coordinator.hpp"

#include <string>

namespace mpcmixer {

// Control-thread-only bridge from applied audio tickets to protocol v1 ACKs.
// The audio callback never calls this class and never performs serialization.
class NativeUiProtocolV1AudioAckPublisher {
public:
    bool publishNext(
        const MixerController& controller,
        NativeUiSessionCoordinator& session,
        std::string& payload) noexcept {
        if (pending_) return false;
        NativeUiAudioAck acknowledgement;
        if (!session.peekApplied(controller, acknowledgement))
            return false;
        try {
            std::string candidate;
            if (!NativeUiProtocolV1ResponseSerializer::audioAck(
                    acknowledgement.connectionEpoch,
                    acknowledgement.sequence,
                    acknowledgement.audioTicket, candidate))
                return false;
            pendingAck_ = acknowledgement;
            pending_ = true;
            payload.swap(candidate);
            return true;
        } catch (...) {
            return false;
        }
    }

    // Consume only after the complete framed response has been sent.
    bool markDelivered(NativeUiSessionCoordinator& session) noexcept {
        if (!pending_ || !session.consumeApplied(pendingAck_)) return false;
        pendingAck_ = NativeUiAudioAck{};
        pending_ = false;
        return true;
    }

    void cancelPending() noexcept {
        pendingAck_ = NativeUiAudioAck{};
        pending_ = false;
    }

    bool hasPendingDelivery() const noexcept { return pending_; }

private:
    NativeUiAudioAck pendingAck_{};
    bool pending_ = false;
};

} // namespace mpcmixer
