#pragma once

#include "native_ui_protocol_v1_audio_ack_publisher.hpp"
#include "native_ui_protocol_v1_client_message_handler.hpp"
#include "native_ui_protocol_v1_command_processor.hpp"
#include "native_ui_protocol_v1_snapshot_delivery.hpp"
#include "native_ui_unix_stream_protocol_owner.hpp"

#include <cstdint>
#include <string>

namespace mpcmixer {

// A bounded, caller-driven transport/protocol adapter. Each public step does
// one unit of work; the embedding event loop decides when to call it.
// This class neither creates sockets nor owns a polling loop or audio callback.
class NativeUiUnixStreamProtocolDriver {
public:
    enum class Status : std::uint8_t {
        Idle,
        FrameReady,
        SnapshotRequested,
        ResponsePrepared,
        Progress,
        WouldBlock,
        Sent,
        Busy,
        Disconnected,
        InvalidFrame,
        ProtocolRejected,
        CommitFailed,
        IoError
    };

    struct Result {
        Status status = Status::Idle;
        std::size_t bytes = 0;
    };

    NativeUiUnixStreamProtocolDriver(
        NativeUiUnixStreamConnection& connection,
        NativeUiUnixStreamProtocolOwner& owner,
        NativeUiProtocolV1ClientMessageHandler& ingress,
        NativeUiProtocolV1CommandProcessor& commands,
        NativeUiProtocolV1SnapshotDelivery& snapshots,
        NativeUiProtocolV1AudioAckPublisher& acknowledgements,
        MixerController& controller,
        MixerUiCommandDispatcher& dispatcher,
        MixerPresentationPublisher& publisher) noexcept
        : connection_(connection), owner_(owner), ingress_(ingress),
          commands_(commands), snapshots_(snapshots),
          acknowledgements_(acknowledgements), controller_(controller),
          dispatcher_(dispatcher), publisher_(publisher) {}

    // Reads incrementally until one frame is ready or the nonblocking socket
    // would block. The caller must dispatch that frame before reading again.
    Result receiveOne() noexcept {
        if (!connection_.connected()) return {Status::Disconnected, 0};
        if (connection_.reader().hasFrame()) return {Status::FrameReady, 0};
        const auto status =
            connection_.reader().receiveNext(connection_.descriptor());
        if (status == NativeUiFrameCodec::Status::FrameReady)
            return {Status::FrameReady, 0};
        if (status == NativeUiFrameCodec::Status::NeedMore)
            return {Status::WouldBlock, 0};
        if (status == NativeUiFrameCodec::Status::InvalidConfiguration ||
            status == NativeUiFrameCodec::Status::InvalidLength)
            return {Status::InvalidFrame, 0};
        return {Status::IoError, 0};
    }

    // Dispatches exactly one complete inbound frame. Hello and snapshot
    // requests use the session handler; command envelopes use the command
    // processor. A staged response must be flushed before another frame is
    // dispatched, preserving the writer's single-frame backpressure.
    Result dispatchOne() noexcept {
        if (!connection_.connected()) return {Status::Disconnected, 0};
        if (!connection_.reader().hasFrame()) return {Status::Idle, 0};
        if (responseStaged_ || connection_.writer().pending())
            return {Status::Busy, 0};

        std::string payload(
            reinterpret_cast<const char*>(connection_.reader().payload()),
            connection_.reader().payloadSize());
        connection_.reader().acknowledgeFrame();

        std::string response;
        const auto handled =
            ingress_.handle(payload, connection_.session(), response);
        if (handled.status ==
            NativeUiProtocolV1ClientMessageStatus::HelloAccepted) {
            stage(response, ResponseKind::Ordinary, 0, 0);
            return {Status::ResponsePrepared, 0};
        }
        if (handled.status ==
            NativeUiProtocolV1ClientMessageStatus::SnapshotRequested) {
            snapshotEpoch_ = handled.epoch;
            snapshotRequest_ = handled.request;
            return {Status::SnapshotRequested, 0};
        }
        if (handled.status ==
                NativeUiProtocolV1ClientMessageStatus::StaleEpoch ||
            handled.status == NativeUiProtocolV1ClientMessageStatus::Busy)
            return {Status::ProtocolRejected, 0};

        const auto command = commands_.process(
            payload, connection_.session(), dispatcher_, response);
        if (command.status == NativeUiProtocolV1CommandProcessStatus::Accepted ||
            command.status == NativeUiProtocolV1CommandProcessStatus::Rejected) {
            stage(response, ResponseKind::Ordinary, 0, 0);
            return {Status::ResponsePrepared, 0};
        }
        return {Status::ProtocolRejected, 0};
    }

    // Prepares at most one deferred response. Snapshot readiness and audio
    // ticket consumption remain pending until flushOne() reports Sent.
    Result prepareNextResponse() noexcept {
        if (!connection_.connected()) return {Status::Disconnected, 0};
        if (responseStaged_ || connection_.writer().pending())
            return {Status::Busy, 0};

        if (snapshotRequest_ != 0) {
            std::string candidate;
            if (!ingress_.prepareSnapshot(
                    snapshotEpoch_, snapshotRequest_, publisher_,
                    connection_.session(), snapshots_, candidate))
                return {Status::Busy, 0};
            stage(candidate, ResponseKind::Snapshot,
                  snapshotEpoch_, snapshots_.pendingSequence());
            snapshotEpoch_ = 0;
            snapshotRequest_ = 0;
            return {Status::ResponsePrepared, 0};
        }

        std::string candidate;
        if (!acknowledgements_.publishNext(
                controller_, connection_.session(), candidate))
            return {Status::Idle, 0};
        stage(candidate, ResponseKind::AudioAck, 0, 0);
        return {Status::ResponsePrepared, 0};
    }

    // Queues and advances at most the currently staged frame. The writer
    // retains partial bytes across WouldBlock; delivery is committed only
    // after the complete frame has been sent.
    Result flushOne() noexcept {
        if (!connection_.connected()) return {Status::Disconnected, 0};
        if (!responseStaged_ && !connection_.writer().pending())
            return {Status::Idle, 0};

        if (responseStaged_ && !connection_.writer().pending()) {
            const auto queued = connection_.writer().queue(
                response_.data(), response_.size());
            if (queued.status != NativeUiUnixStreamFrameWriter::Status::Queued)
                return {Status::Busy, 0};
        }

        const auto flushed =
            connection_.writer().flush(connection_.descriptor());
        if (flushed.status == NativeUiUnixStreamFrameWriter::Status::WouldBlock)
            return {Status::WouldBlock, flushed.bytes};
        if (flushed.status == NativeUiUnixStreamFrameWriter::Status::Progress)
            return {Status::Progress, flushed.bytes};
        if (flushed.status == NativeUiUnixStreamFrameWriter::Status::PeerClosed ||
            flushed.status == NativeUiUnixStreamFrameWriter::Status::Error)
            return {Status::IoError, flushed.bytes};
        if (flushed.status != NativeUiUnixStreamFrameWriter::Status::Sent)
            return {Status::Idle, flushed.bytes};

        bool committed = true;
        if (responseKind_ == ResponseKind::Snapshot) {
            committed = ingress_.markSnapshotDelivered(
                responseEpoch_, responseSequence_, responseSequence_,
                connection_.session(), snapshots_);
        } else if (responseKind_ == ResponseKind::AudioAck) {
            committed = acknowledgements_.markDelivered(connection_.session());
        }
        clearStaged();
        return {committed ? Status::Sent : Status::CommitFailed, flushed.bytes};
    }

    bool disconnect(std::uint64_t epoch) noexcept {
        if (!owner_.disconnect(epoch)) return false;
        clearStaged();
        snapshotEpoch_ = 0;
        snapshotRequest_ = 0;
        return true;
    }

    bool hasStagedResponse() const noexcept { return responseStaged_; }
    bool hasPendingSnapshotRequest() const noexcept {
        return snapshotRequest_ != 0;
    }

private:
    enum class ResponseKind : std::uint8_t { Ordinary, Snapshot, AudioAck };

    void stage(const std::string& response, ResponseKind kind,
               std::uint64_t epoch, std::uint64_t sequence) {
        response_ = response;
        responseKind_ = kind;
        responseEpoch_ = epoch;
        responseSequence_ = sequence;
        responseStaged_ = true;
    }

    void clearStaged() noexcept {
        response_.clear();
        responseKind_ = ResponseKind::Ordinary;
        responseEpoch_ = 0;
        responseSequence_ = 0;
        responseStaged_ = false;
    }

    NativeUiUnixStreamConnection& connection_;
    NativeUiUnixStreamProtocolOwner& owner_;
    NativeUiProtocolV1ClientMessageHandler& ingress_;
    NativeUiProtocolV1CommandProcessor& commands_;
    NativeUiProtocolV1SnapshotDelivery& snapshots_;
    NativeUiProtocolV1AudioAckPublisher& acknowledgements_;
    MixerController& controller_;
    MixerUiCommandDispatcher& dispatcher_;
    MixerPresentationPublisher& publisher_;
    std::string response_;
    std::uint64_t responseEpoch_ = 0;
    std::uint64_t responseSequence_ = 0;
    std::uint64_t snapshotEpoch_ = 0;
    std::uint64_t snapshotRequest_ = 0;
    ResponseKind responseKind_ = ResponseKind::Ordinary;
    bool responseStaged_ = false;
};

} // namespace mpcmixer
