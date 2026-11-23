#include "native_ui_unix_stream_protocol_driver.hpp"
#include "native_ui_frame_codec.hpp"
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
    std::uint8_t frame[NativeUiProtocolV1SnapshotSerializer::kMaximumPayloadBytes +
                       NativeUiFrameCodec::kHeaderBytes]{};
    if (payload.empty() || payload.size() >
            sizeof(frame) - NativeUiFrameCodec::kHeaderBytes)
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
    if (length == 0 ||
        length > NativeUiProtocolV1SnapshotSerializer::kMaximumPayloadBytes)
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
    NativeUiProtocolV1SnapshotDelivery snapshots;
    NativeUiProtocolV1CommandProcessor commands;
    NativeUiProtocolV1AudioAckPublisher acknowledgements;
    NativeUiUnixStreamProtocolOwner owner(
        connection, ingress, snapshots, acknowledgements);
    AudioEngineCommandBridge bridge;
    MixerController controller(bridge);
    MixerUiCommandDispatcher dispatcher(controller);
    MixerPresentationPublisher publisher(controller);
    NativeUiUnixStreamProtocolDriver driver(
        connection, owner, ingress, commands, snapshots, acknowledgements,
        controller, dispatcher, publisher);

    int pair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, pair) == 0);
    std::uint64_t epoch = 0;
    assert(connection.adopt(pair[0], epoch));

    const std::string hello =
        R"({"version":1,"kind":"hello","epoch":0,"client":"touch-mixer"})";
    assert(sendFrame(pair[1], hello));
    assert(driver.receiveOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::FrameReady);
    assert(driver.dispatchOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::ResponsePrepared);
    assert(driver.flushOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::Sent);
    auto response = nlohmann::json::parse(receiveFrame(pair[1]));
    assert(response["kind"] == "hello");
    assert(response["epoch"] == epoch);
    assert(response["status"] == "ready");

    const std::string snapshotRequest =
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
        std::to_string(epoch) + R"(,"request":1})";
    assert(sendFrame(pair[1], snapshotRequest));
    assert(driver.receiveOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::FrameReady);
    assert(driver.dispatchOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::SnapshotRequested);
    assert(driver.hasPendingSnapshotRequest());
    assert(driver.prepareNextResponse().status ==
           NativeUiUnixStreamProtocolDriver::Status::ResponsePrepared);
    assert(!connection.session().isReady(epoch));
    assert(driver.flushOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::Sent);
    response = nlohmann::json::parse(receiveFrame(pair[1]));
    assert(response["kind"] == "snapshot");
    assert(response["epoch"] == epoch);
    assert(response["request"] == 1);
    assert(connection.session().isReady(epoch));

    const std::string command =
        std::string(R"({"version":1,"kind":"command","epoch":)") +
        std::to_string(epoch) +
        R"(,"sequence":1,"type":"SetTrackFader","track":2,"value":81})";
    assert(sendFrame(pair[1], command));
    assert(driver.receiveOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::FrameReady);
    assert(driver.dispatchOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::ResponsePrepared);
    assert(driver.flushOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::Sent);
    response = nlohmann::json::parse(receiveFrame(pair[1]));
    assert(response["kind"] == "command_result");
    assert(response["status"] == "accepted");
    const std::uint32_t ticket =
        response["audio_ticket"].get<std::uint32_t>();
    assert(ticket != 0);
    assert(connection.session().pendingAcknowledgements() == 1);

    const float* inputs[AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    bridge.process(inputs, left, right, 1);
    assert(driver.prepareNextResponse().status ==
           NativeUiUnixStreamProtocolDriver::Status::ResponsePrepared);
    assert(acknowledgements.hasPendingDelivery());
    assert(connection.session().pendingAcknowledgements() == 1);
    assert(driver.flushOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::Sent);
    response = nlohmann::json::parse(receiveFrame(pair[1]));
    assert(response["kind"] == "audio_ack");
    assert(response["epoch"] == epoch);
    assert(response["audio_ticket"] == ticket);
    assert(response["status"] == "applied");
    assert(connection.session().pendingAcknowledgements() == 0);
    assert(!acknowledgements.hasPendingDelivery());

    assert(driver.disconnect(epoch));
    assert(!connection.connected());
    ::close(pair[1]);

    // Reconnect and force a partial outbound snapshot write. Fill the
    // nonblocking socket's send queue, free only a small amount of peer
    // receive space, then verify the driver's staged frame survives EAGAIN
    // and is discarded by epoch teardown.
    int backpressurePair[2] = {-1, -1};
    assert(::socketpair(AF_UNIX, SOCK_STREAM, 0, backpressurePair) == 0);
    int sendBufferBytes = 4096;
    assert(::setsockopt(backpressurePair[0], SOL_SOCKET, SO_SNDBUF,
                        &sendBufferBytes, sizeof(sendBufferBytes)) == 0);
    std::uint64_t backpressureEpoch = 0;
    assert(connection.adopt(backpressurePair[0], backpressureEpoch));
    assert(backpressureEpoch > epoch);

    assert(sendFrame(backpressurePair[1], hello));
    assert(driver.receiveOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::FrameReady);
    assert(driver.dispatchOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::ResponsePrepared);
    assert(driver.flushOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::Sent);
    response = nlohmann::json::parse(receiveFrame(backpressurePair[1]));
    assert(response["kind"] == "hello");
    assert(response["epoch"] == backpressureEpoch);

    const std::string backpressureSnapshotRequest =
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
        std::to_string(backpressureEpoch) + R"(,"request":1})";
    assert(sendFrame(backpressurePair[1], backpressureSnapshotRequest));
    assert(driver.receiveOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::FrameReady);
    assert(driver.dispatchOne().status ==
           NativeUiUnixStreamProtocolDriver::Status::SnapshotRequested);
    assert(driver.prepareNextResponse().status ==
           NativeUiUnixStreamProtocolDriver::Status::ResponsePrepared);
    assert(driver.hasStagedResponse());

    std::uint8_t filler[4096]{};
    bool sendQueueFull = false;
    for (unsigned attempt = 0; attempt < 256; ++attempt) {
        const auto fill = NativeUiUnixStreamIo::transmit(
            connection.descriptor(), filler, sizeof(filler));
        if (fill.status == NativeUiUnixStreamIo::Status::Progress)
            continue;
        assert(fill.status == NativeUiUnixStreamIo::Status::WouldBlock);
        sendQueueFull = true;
        break;
    }
    assert(sendQueueFull);

    std::uint8_t drain[4096]{};
    std::size_t drained = 0;
    while (drained < sizeof(drain)) {
        const ssize_t count = ::recv(backpressurePair[1], drain,
                                     sizeof(drain) - drained, MSG_DONTWAIT);
        if (count > 0) {
            drained += static_cast<std::size_t>(count);
            continue;
        }
        break;
    }
    assert(drained == sizeof(drain));

    // Refill half of the released space so the snapshot cannot fit in the
    // remaining send window; this makes the subsequent flush genuinely
    // partial rather than merely blocked before its first byte.
    std::uint8_t reserve[2048]{};
    const auto reserved = NativeUiUnixStreamIo::transmit(
        connection.descriptor(), reserve, sizeof(reserve));
    assert(reserved.status == NativeUiUnixStreamIo::Status::Progress);
    assert(reserved.bytes == sizeof(reserve));

    const auto partialFlush = driver.flushOne();
    assert(partialFlush.status ==
           NativeUiUnixStreamProtocolDriver::Status::WouldBlock);
    assert(partialFlush.bytes > 0);
    assert(connection.writer().pending());
    assert(connection.writer().remainingBytes() > 0);
    assert(driver.hasStagedResponse());

    assert(driver.disconnect(backpressureEpoch));
    assert(!connection.connected());
    assert(!connection.writer().pending());
    assert(connection.writer().remainingBytes() == 0);
    assert(!driver.hasStagedResponse());
    assert(!driver.hasPendingSnapshotRequest());
    ::close(backpressurePair[1]);
    return 0;
}
