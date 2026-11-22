#include "native_ui_protocol_v1_response_serializer.hpp"
#include "native_ui_session_coordinator.hpp"
#include "native_ui_protocol_v1_snapshot_decoder.hpp"
#include "native_ui_protocol_v1_snapshot_serializer.hpp"

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

    std::uint64_t epoch = 0;
    assert(session.beginConnection(epoch));
    assert(epoch != 0);

    std::string serverHello;
    assert(NativeUiProtocolV1ResponseSerializer::serverHello(epoch, serverHello));
    const auto hello = nlohmann::json::parse(serverHello);
    assert(hello["kind"] == "hello");
    assert(hello["epoch"] == epoch);
    assert(hello["status"] == "ready");

    // A queued control makes the first presentation snapshot not yet applied.
    assert(controller.setTrackFader(0, 55));
    auto snapshot = publisher.publish();
    assert(!snapshot.audioStateCaughtUp);

    std::string wireSnapshot;
    assert(NativeUiProtocolV1SnapshotSerializer::serialize(
        epoch, 1, snapshot, wireSnapshot));
    MixerPresentationSnapshot decoded;
    assert(NativeUiProtocolV1SnapshotDecoder::decode(
        wireSnapshot, epoch, 1, decoded) ==
        NativeUiProtocolV1SnapshotDecodeStatus::Ok);
    assert(session.updateSnapshot(epoch, decoded, true));
    assert(!session.isReady(epoch));

    MixerUiCommand command;
    command.sequence = 1;
    command.type = MixerUiCommandType::SetTrackFader;
    command.track = 2;
    command.value = 81;
    auto result = session.dispatch(epoch, dispatcher, command);
    assert(result.status == MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 0);

    // Callback applies the queued state; a newer full snapshot opens the gate.
    const float* inputs[AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    bridge.process(inputs, left, right, 1);
    snapshot = publisher.publish();
    assert(snapshot.audioStateCaughtUp);
    assert(NativeUiProtocolV1SnapshotSerializer::serialize(
        epoch, 2, snapshot, wireSnapshot));
    assert(NativeUiProtocolV1SnapshotDecoder::decode(
        wireSnapshot, epoch, 2, decoded) ==
        NativeUiProtocolV1SnapshotDecodeStatus::Ok);
    assert(session.updateSnapshot(epoch, decoded, true));
    assert(session.isReady(epoch));

    result = session.dispatch(epoch, dispatcher, command);
    assert(result.accepted());
    assert(result.audioTicket != 0);

    std::string commandResult;
    assert(NativeUiProtocolV1ResponseSerializer::commandResult(
        epoch, result, commandResult));
    const auto commandJson = nlohmann::json::parse(commandResult);
    assert(commandJson["kind"] == "command_result");
    assert(commandJson["epoch"] == epoch);
    assert(commandJson["sequence"] == command.sequence);
    assert(commandJson["status"] == "accepted");
    assert(commandJson["audio_ticket"] == result.audioTicket);

    // No ACK is visible before the audio callback applies the ticket.
    NativeUiAudioAck ack;
    assert(!session.popApplied(controller, ack));
    bridge.process(inputs, left, right, 1);
    assert(session.popApplied(controller, ack));
    assert(ack.sequence == command.sequence);
    assert(ack.connectionEpoch == epoch);
    assert(ack.audioTicket == result.audioTicket);

    std::string audioAck;
    assert(NativeUiProtocolV1ResponseSerializer::audioAck(
        ack.connectionEpoch, ack.sequence, ack.audioTicket, audioAck));
    const auto ackJson = nlohmann::json::parse(audioAck);
    assert(ackJson["kind"] == "audio_ack");
    assert(ackJson["epoch"] == epoch);
    assert(ackJson["sequence"] == command.sequence);
    assert(ackJson["audio_ticket"] == result.audioTicket);
    assert(ackJson["status"] == "applied");

    assert(session.invalidate(epoch));
    assert(!session.isReady(epoch));
    return 0;
}
