#include "native_ui_protocol_v1_audio_ack_publisher.hpp"
#include "native_ui_protocol_v1_command_processor.hpp"
#include "native_ui_protocol_v1_response_serializer.hpp"
#include "native_ui_session_coordinator.hpp"

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
    NativeUiProtocolV1CommandProcessor processor;
    NativeUiProtocolV1AudioAckPublisher ackPublisher;

    std::uint64_t epoch = 0;
    assert(session.beginConnection(epoch));

    std::string response = "keep";
    const std::string command =
        std::string(R"({"version":1,"kind":"command","epoch":)") +
        std::to_string(epoch) +
        R"(,"sequence":1,"type":"SetTrackFader","track":3,"value":76})";

    auto result = processor.process(command, session, dispatcher, response);
    assert(result.status == NativeUiProtocolV1CommandProcessStatus::NotReady);
    assert(response == "keep");
    assert(dispatcher.lastAcceptedSequence() == 0);

    // A complete caught-up snapshot opens the gate.
    const MixerPresentationSnapshot snapshot = publisher.publish();
    assert(snapshot.audioStateCaughtUp);
    assert(session.updateSnapshot(epoch, snapshot, true));
    assert(session.isReady(epoch));

    result = processor.process(command, session, dispatcher, response);
    assert(result.status == NativeUiProtocolV1CommandProcessStatus::Accepted);
    assert(result.commandResult.accepted());
    assert(result.commandResult.sequence == 1);
    assert(result.commandResult.audioTicket != 0);
    const std::uint32_t acceptedAudioTicket = result.commandResult.audioTicket;
    const auto json = nlohmann::json::parse(response);
    assert(json["kind"] == "command_result");
    assert(json["epoch"] == epoch);
    assert(json["sequence"] == 1);
    assert(json["status"] == "accepted");
    assert(json["audio_ticket"] == result.commandResult.audioTicket);

    // A rejected out-of-order command produces a result but consumes no new
    // command sequence in the dispatcher.
    result = processor.process(command, session, dispatcher, response);
    assert(result.status == NativeUiProtocolV1CommandProcessStatus::Rejected);
    assert(result.commandResult.status == MixerUiCommandStatus::OutOfOrder);
    assert(nlohmann::json::parse(response)["status"] == "out_of_order");
    assert(dispatcher.lastAcceptedSequence() == 1);

    const std::string stale =
        R"({"version":1,"kind":"command","epoch":999,"sequence":2,"type":"SetMasterMute","flag":true})";
    const std::string previous = response;
    result = processor.process(stale, session, dispatcher, response);
    assert(result.status == NativeUiProtocolV1CommandProcessStatus::StaleEpoch);
    assert(response == previous);

    const std::string malformed =
        std::string(R"({"version":1,"kind":"command","epoch":)") +
        std::to_string(epoch) +
        R"(,"sequence":2,"type":"SetTrackFader","track":99,"value":20})";
    result = processor.process(malformed, session, dispatcher, response);
    assert(result.status == NativeUiProtocolV1CommandProcessStatus::InvalidMessage);
    assert(response == previous);

    // Audio callback completion is reported separately by the session ACK
    // tracker; command_result is not an audio-applied acknowledgement.
    std::string ackPayload = "keep";
    assert(!ackPublisher.publishNext(controller, session, ackPayload));
    assert(ackPayload == "keep");
    const float* inputs[AudioEngine::kTrackCount]{};
    float left[1]{};
    float right[1]{};
    bridge.process(inputs, left, right, 1);
    assert(ackPublisher.publishNext(controller, session, ackPayload));
    const auto ackJson = nlohmann::json::parse(ackPayload);
    assert(ackJson["kind"] == "audio_ack");
    assert(ackJson["epoch"] == epoch);
    assert(ackJson["sequence"] == 1);
    assert(ackJson["audio_ticket"] == acceptedAudioTicket);
    assert(ackJson["status"] == "applied");
    assert(!ackPublisher.publishNext(controller, session, ackPayload));

    assert(session.invalidate(epoch));
    return 0;
}
