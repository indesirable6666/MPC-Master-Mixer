#pragma once

#include "native_ui_protocol_v1_client_message_decoder.hpp"
#include "native_ui_protocol_v1_response_serializer.hpp"
#include "native_ui_protocol_v1_snapshot_request_tracker.hpp"
#include "native_ui_session_coordinator.hpp"
#include "native_ui_protocol_v1_snapshot_delivery.hpp"

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
            // A transport owner may already have opened the session and
            // assigned its epoch when it adopted the descriptor. In that
            // case, bind protocol request tracking to that epoch instead of
            // attempting to begin a second session.
            const bool transportSessionAlreadyOpen =
                session.activeEpoch() != 0;
            std::uint64_t epoch = session.activeEpoch();
            if (!transportSessionAlreadyOpen &&
                !session.beginConnection(epoch))
                return {NativeUiProtocolV1ClientMessageStatus::Busy, 0, 0};

            if (!requests_.begin(epoch)) {
                if (!transportSessionAlreadyOpen)
                    session.invalidate(epoch);
                return {NativeUiProtocolV1ClientMessageStatus::Busy, 0, 0};
            }

            std::string candidate;
            if (!NativeUiProtocolV1ResponseSerializer::serverHello(
                    epoch, candidate)) {
                requests_.invalidate(epoch);
                if (!transportSessionAlreadyOpen)
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
        // This v1 implementation deliberately permits one snapshot request
        // in flight. Do not consume a later request identifier until the
        // prior request has been prepared and its frame fully delivered.
        if (snapshotRequestAwaitingPreparation_ || preparedRequest_ != 0)
            return {NativeUiProtocolV1ClientMessageStatus::Busy,
                    message.epoch, message.request};
        if (!requests_.accept(message.epoch, message.request))
            return {NativeUiProtocolV1ClientMessageStatus::InvalidMessage,
                    message.epoch, message.request};

        snapshotRequestAwaitingPreparation_ = true;
        return {NativeUiProtocolV1ClientMessageStatus::SnapshotRequested,
                message.epoch, message.request};
    }

    // Prepare only the exact request most recently accepted by handle().
    // A successful prepare moves the single in-flight request to delivery.
    bool prepareSnapshot(std::uint64_t epoch, std::uint64_t request,
                         MixerPresentationPublisher& publisher,
                         const NativeUiSessionCoordinator& session,
                         NativeUiProtocolV1SnapshotDelivery& delivery,
                         std::string& output) {
        if (!snapshotRequestAwaitingPreparation_ || request == 0 ||
            request != requests_.lastRequest() || epoch == 0 ||
            epoch != requests_.activeEpoch() || delivery.hasPendingDelivery())
            return false;
        if (!delivery.prepare(epoch, request, publisher, session, output))
            return false;
        snapshotRequestAwaitingPreparation_ = false;
        preparedRequest_ = request;
        preparedEpoch_ = epoch;
        return true;
    }

    // Commit delivery only after the transport reports the complete frame.
    bool markSnapshotDelivered(std::uint64_t epoch, std::uint64_t request,
                               std::uint64_t sequence,
                               NativeUiSessionCoordinator& session,
                               NativeUiProtocolV1SnapshotDelivery& delivery) noexcept {
        if (preparedRequest_ == 0 || request != preparedRequest_ ||
            epoch == 0 || epoch != preparedEpoch_)
            return false;
        if (!delivery.markDelivered(epoch, sequence, session))
            return false;
        preparedRequest_ = 0;
        preparedEpoch_ = 0;
        return true;
    }

    bool invalidate(std::uint64_t epoch,
                    NativeUiSessionCoordinator& session) noexcept {
        if (epoch == 0 || epoch != session.activeEpoch()) return false;
        const bool requestClosed = requests_.invalidate(epoch);
        snapshotRequestAwaitingPreparation_ = false;
        preparedRequest_ = 0;
        preparedEpoch_ = 0;
        const bool sessionClosed = session.invalidate(epoch);
        return requestClosed && sessionClosed;
    }

    // Clear protocol-owned request and snapshot state while leaving session
    // invalidation to the transport connection owner. The transport must
    // discard its queued frame before calling this method.
    bool resetConnectionState(
        std::uint64_t epoch,
        NativeUiProtocolV1SnapshotDelivery& delivery) noexcept {
        if (epoch == 0 || epoch != requests_.activeEpoch())
            return false;
        const bool requestClosed = requests_.invalidate(epoch);
        snapshotRequestAwaitingPreparation_ = false;
        preparedRequest_ = 0;
        preparedEpoch_ = 0;
        delivery.cancelPending(epoch);
        return requestClosed;
    }

    // Disconnect variant that also releases an unsent snapshot owned by this
    // connection. The transport must discard its queued frame before calling.
    bool invalidate(std::uint64_t epoch,
                    NativeUiSessionCoordinator& session,
                    NativeUiProtocolV1SnapshotDelivery& delivery) noexcept {
        if (epoch == 0 || epoch != session.activeEpoch()) return false;
        requests_.invalidate(epoch);
        snapshotRequestAwaitingPreparation_ = false;
        preparedRequest_ = 0;
        preparedEpoch_ = 0;
        delivery.cancelPending(epoch);
        return session.invalidate(epoch);
    }

    std::uint64_t lastSnapshotRequest() const noexcept {
        return requests_.lastRequest();
    }

private:
    NativeUiProtocolV1SnapshotRequestTracker requests_;
    std::uint64_t preparedEpoch_ = 0;
    std::uint64_t preparedRequest_ = 0;
    bool snapshotRequestAwaitingPreparation_ = false;
};

} // namespace mpcmixer
