#pragma once

#include "native_ui_protocol_v1_client_message_decoder.hpp"
#include "native_ui_protocol_v1_response_serializer.hpp"
#include "native_ui_protocol_v1_snapshot_request_tracker.hpp"
#include "native_ui_session_coordinator.hpp"

#include <cstdint>
#include <string>

namespace mpcmixer {

enum class NativeUiProtocolV1ClientMessageStatus : std::uint8_t {
    HelloAccepted,
    SnapshotRequested,
    InvalidMessage,
    StaleEpoch,
    Busy
};

struct NativeUiProtocolV1ClientMessageResult {
    NativeUiProtocolV1ClientMessageStatus status =
        NativeUiProtocolV1ClientMessageStatus::InvalidMessage;
    std::uint64_t epoch = 0;
    std::uint64_t request = 0;
};

// Control-thread-only session ingress. It handles only client hello and
// snapshot_request messages; command dispatch and socket I/O remain outside.
class NativeUiProtocolV1ClientMessageHandler {
public:
    NativeUiProtocolV1ClientMessageResult handle(
        const std::string& payload,
        NativeUiSessionCoordinator& session,
        std::string& response) {
        NativeUiProtocolV1ClientMessage message;
        if (NativeUiProtocolV1ClientMessageDecoder::decode(payload, message) !=
            NativeUiProtocolV1ClientDecodeStatus::Ok)
            return {NativeUiProtocolV1ClientMessageStatus::InvalidMessage, 0, 0};

        if (message.kind == NativeUiProtocolV1ClientMessageKind::Hello) {
            if (session.activeEpoch() != 0)
                return {NativeUiProtocolV1ClientMessageStatus::Busy, 0, 0};

            std::uint64_t epoch = 0;
            if (!session.beginConnection(epoch))
                return {NativeUiProtocolV1ClientMessageStatus::Busy, 0, 0};

            if (!requests_.begin(epoch)) {
                session.invalidate(epoch);
                return {NativeUiProtocolV1ClientMessageStatus::Busy, 0, 0};
            }

            std::string candidate;
            if (!NativeUiProtocolV1ResponseSerializer::serverHello(
                    epoch, candidate)) {
                requests_.invalidate(epoch);
                session.invalidate(epoch);
                return {NativeUiProtocolV1ClientMessageStatus::Busy, 0, 0};
            }
            response.swap(candidate);
            return {NativeUiProtocolV1ClientMessageStatus::HelloAccepted,
                    epoch, 0};
        }

        const std::uint64_t active = session.activeEpoch();
        if (active == 0 || message.epoch != active)
            return {NativeUiProtocolV1ClientMessageStatus::StaleEpoch,
                    message.epoch, message.request};
        if (!requests_.accept(message.epoch, message.request))
            return {NativeUiProtocolV1ClientMessageStatus::InvalidMessage,
                    message.epoch, message.request};

        return {NativeUiProtocolV1ClientMessageStatus::SnapshotRequested,
                message.epoch, message.request};
    }

    bool invalidate(std::uint64_t epoch,
                    NativeUiSessionCoordinator& session) noexcept {
        if (epoch == 0 || epoch != session.activeEpoch()) return false;
        const bool requestClosed = requests_.invalidate(epoch);
        const bool sessionClosed = session.invalidate(epoch);
        return requestClosed && sessionClosed;
    }

    std::uint64_t lastSnapshotRequest() const noexcept {
        return requests_.lastRequest();
    }

private:
    NativeUiProtocolV1SnapshotRequestTracker requests_;
};

} // namespace mpcmixer
