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
    assert(ackPublisher.markDelivered(session));
    assert(!ackPublisher.publishNext(controller, session, ackPayload));

    // Audio In and track DSP commands traverse the strict v1 wire decoder,
    // readiness gate, typed adapter, dispatcher and acknowledgement ledger.
    const auto payloadFor = [&](std::uint64_t sequence, const std::string& fields) {
        return std::string(R"({"version":1,"kind":"command","epoch":)") +
            std::to_string(epoch) + R"(,"sequence":)" +
            std::to_string(sequence) + "," + fields + "}";
    };
    const auto submit = [&](const std::string& fields, std::uint64_t sequence) {
        const auto submitted = processor.process(
            payloadFor(sequence, fields), session, dispatcher, response);
        assert(submitted.status == NativeUiProtocolV1CommandProcessStatus::Accepted);
        assert(submitted.commandResult.accepted());
        assert(submitted.commandResult.sequence == sequence);
        assert(submitted.commandResult.audioTicket != 0);
        const auto reply = nlohmann::json::parse(response);
        assert(reply["kind"] == "command_result");
        assert(reply["epoch"] == epoch);
        assert(reply["sequence"] == sequence);
        assert(reply["status"] == "accepted");
        return submitted.commandResult.audioTicket;
    };

    const std::uint32_t linkTicket =
        submit(R"("type":"SetAudioInputPairLinked","flag":true)", 2);
    const std::uint32_t gainTicket =
        submit(R"("type":"SetAudioInputPairStereoGain","value":88)", 3);
    const std::uint32_t balanceTicket =
        submit(R"("type":"SetAudioInputPairBalance","value":-35)", 4);
    const std::uint32_t muteTicket =
        submit(R"("type":"SetAudioInputPairStereoMute","flag":true)", 5);
    const std::uint32_t soloTicket =
        submit(R"("type":"SetAudioInputChannelSolo","track":1,"flag":true)", 6);
    const std::uint32_t auxTicket =
        submit(R"("type":"SetAudioInputChannelAuxSend","track":0,"index":1,"value":65)", 7);
    const std::uint32_t linkedEqTicket =
        submit(R"("type":"SetAudioInputChannelEqBand","track":0,"index":2,"frequencyHz":1000,"q":0.8,"gainDb":3,"sampleRate":48000)", 8);

    // Pan is not meaningful while the pair is linked. Rejection must leave
    // sequence 9 available for the following LINK transition.
    result = processor.process(payloadFor(
        9, R"("type":"SetAudioInputChannelPan","track":1,"value":-20)"),
        session, dispatcher, response);
    assert(result.status == NativeUiProtocolV1CommandProcessStatus::Rejected);
    assert(result.commandResult.status == MixerUiCommandStatus::Invalid);
    assert(dispatcher.lastAcceptedSequence() == 8);

    const std::uint32_t unlinkTicket =
        submit(R"("type":"SetAudioInputPairLinked","flag":false)", 9);
    const std::uint32_t panTicket =
        submit(R"("type":"SetAudioInputChannelPan","track":1,"value":-20)", 10);
    const std::uint32_t channelGainTicket =
        submit(R"("type":"SetAudioInputChannelGain","track":0,"value":74)", 11);
    const std::uint32_t channelMuteTicket =
        submit(R"("type":"SetAudioInputChannelMute","track":1,"flag":false)", 12);
    const std::uint32_t channelSoloTicket =
        submit(R"("type":"SetAudioInputChannelSolo","track":0,"flag":false)", 13);
    const std::uint32_t monoEqTicket =
        submit(R"("type":"SetAudioInputChannelEqBand","track":0,"index":3,"frequencyHz":1500,"q":1,"gainDb":-1,"sampleRate":96000)", 14);
    const std::uint32_t trackAuxTicket =
        submit(R"("type":"SetTrackAuxSend","track":3,"index":2,"value":45)", 15);
    const std::uint32_t trackEqTicket =
        submit(R"("type":"SetTrackEqBand","track":3,"index":1,"frequencyHz":1200,"q":0.8,"gainDb":-2.5,"sampleRate":96000)", 16);

    // All commands are applied at a block boundary. The snapshot exposes
    // desired values while each audio ACK remains FIFO and separately gated.
    bridge.process(inputs, left, right, 1);
    const auto snapshotAfterCommands = publisher.publish();
    const auto& audioInputSnapshot = snapshotAfterCommands.audioInputPair;
    assert(!audioInputSnapshot.linked);
    assert(audioInputSnapshot.stereoGainValue == 88);
    assert(audioInputSnapshot.balancePercent == -35);
    assert(audioInputSnapshot.stereoMute);
    assert(audioInputSnapshot.stereoSolo);
    assert(audioInputSnapshot.stereoAuxSendPercent[1] == 65);
    assert(audioInputSnapshot.channels[0].gainValue == 74);
    assert(audioInputSnapshot.channels[1].gainValue == 88);
    assert(audioInputSnapshot.channels[0].mute);
    assert(!audioInputSnapshot.channels[1].mute);
    assert(!audioInputSnapshot.channels[0].solo);
    assert(audioInputSnapshot.channels[1].solo);
    assert(audioInputSnapshot.channels[1].panPercent == -20);
    assert(audioInputSnapshot.channels[0].auxSendPercent[1] == 65);
    assert(audioInputSnapshot.channels[1].auxSendPercent[1] == 65);
    assert(audioInputSnapshot.channels[0].eqSampleRate == 96000.0f);
    assert(audioInputSnapshot.channels[1].eqSampleRate == 48000.0f);
    assert(audioInputSnapshot.channels[0].eq[3].frequencyHz == 1500.0f);
    assert(audioInputSnapshot.channels[1].eq[2].gainDb == 3.0f);
    assert(snapshotAfterCommands.tracks[3].auxSendPercent[2] == 45);
    assert(snapshotAfterCommands.tracks[3].eqConfigured);
    assert(snapshotAfterCommands.tracks[3].eqSampleRate == 96000.0f);
    assert(snapshotAfterCommands.tracks[3].eq[1].gainDb == -2.5f);

    const std::uint64_t expectedSequences[] = {
        2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16
    };
    const std::uint32_t expectedTickets[] = {
        linkTicket, gainTicket, balanceTicket, muteTicket, soloTicket,
        auxTicket, linkedEqTicket, unlinkTicket, panTicket, channelGainTicket,
        channelMuteTicket, channelSoloTicket, monoEqTicket, trackAuxTicket,
        trackEqTicket
    };
    for (std::size_t i = 0; i < 15; ++i) {
        assert(ackPublisher.publishNext(controller, session, ackPayload));
        const auto ack = nlohmann::json::parse(ackPayload);
        assert(ack["kind"] == "audio_ack");
        assert(ack["epoch"] == epoch);
        assert(ack["sequence"] == expectedSequences[i]);
        assert(ack["audio_ticket"] == expectedTickets[i]);
        assert(ack["status"] == "applied");
        assert(ackPublisher.markDelivered(session));
    }
    assert(!ackPublisher.publishNext(controller, session, ackPayload));

    assert(session.invalidate(epoch));
    return 0;
}
