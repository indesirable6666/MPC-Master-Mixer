#include "native_ui_protocol_v1_client_message_handler.hpp"
#include "native_ui_protocol_v1_command_processor.hpp"
#include "native_ui_protocol_v1_audio_ack_publisher.hpp"
#include "native_ui_protocol_v1_response_serializer.hpp"
#include "native_ui_protocol_v1_snapshot_decoder.hpp"
#include "native_ui_protocol_v1_snapshot_delivery.hpp"

#include "../../third_party/nlohmann/json.hpp"

#include <cassert>
#include <cstdint>
#include <string>

using namespace mpcmixer;

int main() {
    AudioEngineCommandBridge bridge;
    MixerController controller(bridge);
    MixerUiCommandDispatcher dispatcher(controller);
    MixerPresentationPublisher publisher(controller);
    NativeUiSessionCoordinator session;
    NativeUiProtocolV1ClientMessageHandler ingress;
    NativeUiProtocolV1SnapshotDelivery delivery;
    NativeUiProtocolV1CommandProcessor commands;
    NativeUiProtocolV1AudioAckPublisher acknowledgements;

    std::string outbound;
    auto message = ingress.handle(
        R"({"version":1,"kind":"hello","epoch":0,"client":"touch-mixer"})",
        session, outbound);
    assert(message.status == NativeUiProtocolV1ClientMessageStatus::HelloAccepted);
    const std::uint64_t epoch = message.epoch;
    assert(epoch != 0);
    auto hello = nlohmann::json::parse(outbound);
    assert(hello["kind"] == "hello" && hello["epoch"] == epoch);

    // First full snapshot reflects a queued audio change, so delivery alone
    // must not open the command gate.
    assert(controller.setTrackFader(0, 55));
    const std::string requestOne =
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
        std::to_string(epoch) + R"(,"request":1})";
    message = ingress.handle(requestOne, session, outbound);
    assert(message.status == NativeUiProtocolV1ClientMessageStatus::SnapshotRequested);
    assert(ingress.prepareSnapshot(epoch, message.request, publisher, session,
                                   delivery, outbound));
    const std::uint64_t firstSequence = delivery.pendingSequence();

    MixerPresentationSnapshot decoded;
    assert(NativeUiProtocolV1SnapshotDecoder::decode(
        outbound, epoch, message.request, decoded) ==
        NativeUiProtocolV1SnapshotDecodeStatus::Ok);
    assert(!decoded.audioStateCaughtUp);
    assert(ingress.markSnapshotDelivered(epoch, message.request, firstSequence,
                                         session, delivery));
    assert(!session.isReady(epoch));

    const std::string command =
        std::string(R"({"version":1,"kind":"command","epoch":)") +
        std::to_string(epoch) +
        R"(,"sequence":1,"type":"SetTrackFader","track":2,"value":81})";
    auto commandResult = commands.process(command, session, dispatcher, outbound);
    assert(commandResult.status == NativeUiProtocolV1CommandProcessStatus::NotReady);

    // Simulate the audio callback applying the earlier ticket. A newer full
    // snapshot is then delivered before commands are admitted.
    const float* inputs[AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    bridge.process(inputs, left, right, 1);

    const std::string requestTwo =
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
        std::to_string(epoch) + R"(,"request":2})";
    message = ingress.handle(requestTwo, session, outbound);
    assert(message.status == NativeUiProtocolV1ClientMessageStatus::SnapshotRequested);
    assert(delivery.prepare(epoch, message.request, publisher, session, outbound));
    const std::uint64_t secondSequence = delivery.pendingSequence();
    assert(secondSequence > firstSequence);
    assert(ingress.markSnapshotDelivered(epoch, message.request, secondSequence,
                                         session, delivery));
    assert(session.isReady(epoch));

    commandResult = commands.process(command, session, dispatcher, outbound);
    assert(commandResult.status == NativeUiProtocolV1CommandProcessStatus::Accepted);
    const auto accepted = nlohmann::json::parse(outbound);
    assert(accepted["kind"] == "command_result");
    assert(accepted["epoch"] == epoch);
    assert(accepted["sequence"] == 1);
    assert(accepted["status"] == "accepted");

    // Acceptance and actual audio application remain separate events.
    outbound = "keep";
    assert(!acknowledgements.publishNext(controller, session, outbound));
    assert(outbound == "keep");
    bridge.process(inputs, left, right, 1);
    NativeUiAudioAck inspectedAck;
    const std::size_t pendingBeforePublish = session.pendingAcknowledgements();
    assert(session.peekApplied(controller, inspectedAck));
    assert(inspectedAck.connectionEpoch == epoch);
    assert(inspectedAck.sequence == 1);
    assert(session.pendingAcknowledgements() == pendingBeforePublish);
    assert(acknowledgements.publishNext(controller, session, outbound));
    assert(session.pendingAcknowledgements() + 1 == pendingBeforePublish);
    const auto applied = nlohmann::json::parse(outbound);
    assert(applied["kind"] == "audio_ack");
    assert(applied["epoch"] == epoch);
    assert(applied["sequence"] == 1);
    assert(applied["audio_ticket"] == commandResult.commandResult.audioTicket);
    assert(applied["status"] == "applied");

    // A prepared but unsent snapshot belongs to the connection too.
    const std::string requestThree =
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
        std::to_string(epoch) + R"(,"request":3})";
    message = ingress.handle(requestThree, session, outbound);
    assert(message.status == NativeUiProtocolV1ClientMessageStatus::SnapshotRequested);
    assert(delivery.prepare(epoch, message.request, publisher, session, outbound));
    assert(delivery.hasPendingDelivery());

    // Disconnect invalidates the session, request tracker, and pending
    // snapshot together. The transport owner discards its queued old-epoch
    // frame before invoking this overload.
    assert(ingress.invalidate(epoch, session, delivery));
    assert(!delivery.hasPendingDelivery());
    assert(!session.isReady(epoch));
    assert(session.activeEpoch() == 0);
    return 0;
}
