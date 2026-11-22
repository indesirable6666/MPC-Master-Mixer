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
        std::string& payload) const noexcept {
        NativeUiAudioAck acknowledgement;
        if (!session.popApplied(controller, acknowledgement))
            return false;

        std::string candidate;
        if (!NativeUiProtocolV1ResponseSerializer::audioAck(
                acknowledgement.connectionEpoch,
                acknowledgement.sequence,
                acknowledgement.audioTicket,
                candidate))
            return false;

        payload.swap(candidate);
        return true;
    }
};

} // namespace mpcmixer
