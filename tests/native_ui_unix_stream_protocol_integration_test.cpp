#include "native_ui_protocol_v1_client_message_handler.hpp"
#include "native_ui_protocol_v1_snapshot_delivery.hpp"
#include "native_ui_unix_stream_connection.hpp"
#include "native_ui_frame_codec.hpp"
#include "native_ui_protocol_v1_snapshot_decoder.hpp"

#include "../../third_party/nlohmann/json.hpp"

#include <cassert>
#include <cstdint>
#include <cstring>
#include <string>
#include <sys/socket.h>
#include <unistd.h>

using namespace mpcmixer;

namespace {

bool sendFrame(int descriptor, const std::string& payload) {
    std::uint8_t frame[NativeUiProtocolV1SnapshotSerializer::kMaximumPayloadBytes + NativeUiFrameCodec::kHeaderBytes]{};
    if (payload.empty() || payload.size() > sizeof(frame) -
            NativeUiFrameCodec::kHeaderBytes)
        return false;
    std::uint8_t header[NativeUiFrameCodec::kHeaderBytes]{};
    if (!NativeUiFrameCodec::encodeLength(
            static_cast<std::uint32_t>(payload.size()), header))
        return false;
    std::memcpy(frame, header, sizeof(header));
    std::memcpy(frame + sizeof(header), payload.data(), payload.size());
    const std::size_t total = sizeof(header) + payload.size();
    return ::send(descriptor, frame, total, 0) ==
           static_cast<ssize_t>(total);
}

std::string receiveFrame(int descriptor) {
    std::uint8_t header[NativeUiFrameCodec::kHeaderBytes]{};
    if (::recv(descriptor, header, sizeof(header), MSG_WAITALL) !=
        static_cast<ssize_t>(sizeof(header)))
        return {};
    const std::uint32_t length = NativeUiFrameCodec::decodeLength(header);
    if (length == 0 || length > NativeUiProtocolV1SnapshotSerializer::kMaximumPayloadBytes)
        return {};
    std::string payload(length, '\0');
    if (::recv(descriptor, &payload[0], length, MSG_WAITALL) !=
        static_cast<ssize_t>(length))
        return {};
    return payload;
}

} // namespace

int main() {
    std::uint8_t txStorage[NativeUiProtocolV1SnapshotSerializer::kMaximumPayloadBytes +
                          NativeUiFrameCodec::kHeaderBytes]{};
    NativeUiUnixStreamFrameWriter writer(
        txStorage, sizeof(txStorage),
        NativeUiProtocolV1SnapshotSerializer::kMaximumPayloadBytes);
    std::uint8_t rxPayload[NativeUiProtocolV1SnapshotSerializer::kMaximumPayloadBytes]{};
    std::uint8_t rxScratch[4096]{};
    NativeUiUnixStreamFrameReaderPump reader(
        rxPayload, sizeof(rxPayload),
        NativeUiProtocolV1SnapshotSerializer::kMaximumPayloadBytes,
        rxScratch, sizeof(rxScratch));
    NativeUiUnixStreamConnection connection(writer, reader);
    NativeUiProtocolV1ClientMessageHandler ingress;
    NativeUiProtocolV1SnapshotDelivery delivery;
    AudioEngineCommandBridge bridge;
    MixerController controller(bridge);
    MixerPresentationPublisher publisher(controller);

    int pair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    std::uint64_t epoch = 0;
    assert(connection.adopt(pair[0], epoch));
    assert(epoch != 0);

    const std::string hello =
        R"({"version":1,"kind":"hello","epoch":0,"client":"touch-mixer"})";
    assert(sendFrame(pair[1], hello));
    assert(reader.receiveNext(connection.descriptor()) ==
           NativeUiFrameCodec::Status::FrameReady);
    const std::string request(
        reinterpret_cast<const char*>(reader.payload()),
        reader.payloadSize());

    std::string response;
    const auto handled = ingress.handle(request, connection.session(), response);
    assert(handled.status == NativeUiProtocolV1ClientMessageStatus::HelloAccepted);
    assert(handled.epoch == epoch);
    reader.acknowledgeFrame();

    assert(writer.queue(response.data(), response.size()).status ==
           NativeUiUnixStreamFrameWriter::Status::Queued);
    assert(writer.flush(connection.descriptor()).status ==
           NativeUiUnixStreamFrameWriter::Status::Sent);
    const auto serverHello = nlohmann::json::parse(receiveFrame(pair[1]));
    assert(serverHello["kind"] == "hello");
    assert(serverHello["epoch"] == epoch);
    assert(serverHello["status"] == "ready");

    // Protocol state is cleared first; the transport owner then closes the
    // descriptor and invalidates the shared session exactly once.
    assert(ingress.resetConnectionState(epoch, delivery));
    assert(connection.invalidate(epoch));
    assert(connection.session().activeEpoch() == 0);
    ::close(pair[1]);

    // The same protocol handler can bind a fresh hello to a fresh transport
    // epoch after reconnect, with no stale request tracker state.
    int reconnect[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, reconnect) == 0);
    std::uint64_t nextEpoch = 0;
    assert(connection.adopt(reconnect[0], nextEpoch));
    assert(nextEpoch > epoch);
    assert(sendFrame(reconnect[1], hello));
    assert(reader.receiveNext(connection.descriptor()) ==
           NativeUiFrameCodec::Status::FrameReady);
    const std::string reconnectRequest(
        reinterpret_cast<const char*>(reader.payload()),
        reader.payloadSize());
    response.clear();
    const auto reconnected =
        ingress.handle(reconnectRequest, connection.session(), response);
    assert(reconnected.status ==
           NativeUiProtocolV1ClientMessageStatus::HelloAccepted);
    assert(reconnected.epoch == nextEpoch);
    reader.acknowledgeFrame();

    // A second hello cannot replace the active protocol epoch, and a
    // delayed request from the previous transport epoch remains stale.
    const std::string helloResponse = response;
    const auto duplicateHello =
        ingress.handle(hello, connection.session(), response);
    assert(duplicateHello.status ==
           NativeUiProtocolV1ClientMessageStatus::Busy);
    assert(response == helloResponse);

    const std::string staleSnapshotRequest =
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
        std::to_string(epoch) + R"(,"request":1})";
    const auto staleRequest =
        ingress.handle(staleSnapshotRequest, connection.session(), response);
    assert(staleRequest.status ==
           NativeUiProtocolV1ClientMessageStatus::StaleEpoch);

    const std::string currentSnapshotRequest =
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
        std::to_string(nextEpoch) + R"(,"request":1})";
    assert(sendFrame(reconnect[1], currentSnapshotRequest));
    assert(reader.receiveNext(connection.descriptor()) ==
           NativeUiFrameCodec::Status::FrameReady);
    const std::string currentRequestPayload(
        reinterpret_cast<const char*>(reader.payload()),
        reader.payloadSize());
    const auto currentRequest =
        ingress.handle(currentRequestPayload, connection.session(), response);
    assert(currentRequest.status ==
           NativeUiProtocolV1ClientMessageStatus::SnapshotRequested);
    assert(ingress.lastSnapshotRequest() == 1);

    // Complete snapshot path: receive request, prepare from the publisher,
    // queue and fully write the frame, decode it at the peer, then commit
    // readiness only after the transport reports Sent.
    reader.acknowledgeFrame();
    assert(ingress.prepareSnapshot(nextEpoch, currentRequest.request,
                                   publisher, connection.session(), delivery,
                                   response));
    const std::uint64_t snapshotSequence = delivery.pendingSequence();
    assert(snapshotSequence != 0);
    assert(delivery.hasPendingDelivery());
    assert(!connection.session().isReady(nextEpoch));
    assert(writer.queue(response.data(), response.size()).status ==
           NativeUiUnixStreamFrameWriter::Status::Queued);
    const auto snapshotWrite = writer.flush(connection.descriptor());
    assert(snapshotWrite.status == NativeUiUnixStreamFrameWriter::Status::Sent);
    assert(!writer.pending());

    const std::string snapshotPayload = receiveFrame(reconnect[1]);
    assert(!snapshotPayload.empty());
    const auto snapshotJson = nlohmann::json::parse(snapshotPayload);
    assert(snapshotJson["kind"] == "snapshot");
    assert(snapshotJson["version"] == 1);
    assert(snapshotJson["epoch"] == nextEpoch);
    assert(snapshotJson["request"] == 1);
    assert(snapshotJson["sequence"] == snapshotSequence);
    assert(snapshotJson["state"].is_object());
    assert(snapshotJson["state"]["tracks"].is_array());
    assert(snapshotJson["state"]["tracks"].size() == MixerPresentationSnapshot::kTrackCount);
    assert(ingress.markSnapshotDelivered(nextEpoch, currentRequest.request,
                                         snapshotSequence,
                                         connection.session(), delivery));
    assert(!delivery.hasPendingDelivery());
    assert(connection.session().isReady(nextEpoch));

    // A prepared but unsent snapshot is cancelled with its connection and
    // must never make the session ready.
    const std::string secondSnapshotRequest =
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
        std::to_string(nextEpoch) + R"(,"request":2})";
    const auto secondRequest =
        ingress.handle(secondSnapshotRequest, connection.session(), response);
    assert(secondRequest.status ==
           NativeUiProtocolV1ClientMessageStatus::SnapshotRequested);
    assert(ingress.prepareSnapshot(nextEpoch, secondRequest.request,
                                   publisher, connection.session(), delivery,
                                   response));
    assert(delivery.hasPendingDelivery());
    assert(writer.queue(response.data(), response.size()).status ==
           NativeUiUnixStreamFrameWriter::Status::Queued);

    // Reset protocol state and invalidate transport: queued bytes are dropped,
    // pending delivery is cancelled, and the session is closed.
    assert(ingress.resetConnectionState(nextEpoch, delivery));
    assert(!delivery.hasPendingDelivery());
    assert(connection.invalidate(nextEpoch));
    assert(!writer.pending());
    assert(connection.session().activeEpoch() == 0);
    ::close(reconnect[1]);
    return 0;
}
