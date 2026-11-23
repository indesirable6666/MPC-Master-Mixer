#include "native_ui_protocol_v1_client_message_handler.hpp"
#include "native_ui_protocol_v1_snapshot_delivery.hpp"

#include <cassert>
#include <cstdint>
#include <string>

using namespace mpcmixer;

int main() {
    NativeUiProtocolV1ClientMessageHandler handler;
    NativeUiSessionCoordinator session;
    AudioEngineCommandBridge bridge;
    MixerController controller(bridge);
    MixerPresentationPublisher publisher(controller);
    NativeUiProtocolV1SnapshotDelivery delivery;
    std::string response = "unchanged";

    auto result = handler.handle(
        R"({"version":1,"kind":"hello","epoch":0,"client":"touch-mixer"})",
        session, response);
    assert(result.status == NativeUiProtocolV1ClientMessageStatus::HelloAccepted);
    assert(result.epoch != 0);
    assert(result.request == 0);
    assert(response.find("\"kind\":\"hello\"") != std::string::npos);
    const std::uint64_t epoch = result.epoch;

    const std::string helloAgain =
        R"({"version":1,"kind":"hello","epoch":0,"client":"touch-mixer"})";
    const std::string priorResponse = response;
    result = handler.handle(helloAgain, session, response);
    assert(result.status == NativeUiProtocolV1ClientMessageStatus::Busy);
    assert(response == priorResponse);

    result = handler.handle(
        R"({"version":1,"kind":"snapshot_request","epoch":999,"request":1})",
        session, response);
    assert(result.status == NativeUiProtocolV1ClientMessageStatus::StaleEpoch);

    result = handler.handle(
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
            std::to_string(epoch) + R"(,"request":1})",
        session, response);
    assert(result.status == NativeUiProtocolV1ClientMessageStatus::SnapshotRequested);
    assert(result.epoch == epoch && result.request == 1);
    assert(handler.lastSnapshotRequest() == 1);

    result = handler.handle(
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
            std::to_string(epoch) + R"(,"request":1})",
        session, response);
    assert(result.status == NativeUiProtocolV1ClientMessageStatus::Busy);
    assert(handler.lastSnapshotRequest() == 1);

    std::string snapshotWire = "preserve";
    assert(!handler.prepareSnapshot(epoch, 2, publisher, session, delivery,
                                    snapshotWire));
    assert(snapshotWire == "preserve");
    assert(handler.prepareSnapshot(epoch, 1, publisher, session, delivery,
                                   snapshotWire));
    const std::uint64_t firstSnapshotSequence = delivery.pendingSequence();
    assert(!handler.prepareSnapshot(epoch, 1, publisher, session, delivery,
                                    snapshotWire));
    assert(handler.markSnapshotDelivered(epoch, 1, firstSnapshotSequence,
                                         session, delivery));
    assert(!delivery.hasPendingDelivery());
    result = handler.handle(
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
            std::to_string(epoch) + R"(,"request":1})",
        session, response);
    assert(result.status == NativeUiProtocolV1ClientMessageStatus::InvalidMessage);
    assert(handler.lastSnapshotRequest() == 1);

    result = handler.handle(
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
            std::to_string(epoch) + R"(,"request":3})",
        session, response);
    assert(result.status == NativeUiProtocolV1ClientMessageStatus::SnapshotRequested);
    assert(handler.lastSnapshotRequest() == 3);
    result = handler.handle(
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
            std::to_string(epoch) + R"(,"request":4})",
        session, response);
    assert(result.status == NativeUiProtocolV1ClientMessageStatus::Busy);
    assert(handler.lastSnapshotRequest() == 3);
    assert(handler.prepareSnapshot(epoch, 3, publisher, session, delivery,
                                   snapshotWire));
    const std::uint64_t thirdSnapshotSequence = delivery.pendingSequence();
    assert(handler.markSnapshotDelivered(epoch, 3, thirdSnapshotSequence,
                                         session, delivery));

    assert(handler.invalidate(epoch, session));
    assert(session.activeEpoch() == 0);
    assert(handler.lastSnapshotRequest() == 0);

    result = handler.handle(
        std::string(R"({"version":1,"kind":"snapshot_request","epoch":)") +
            std::to_string(epoch) + R"(,"request":4})",
        session, response);
    assert(result.status == NativeUiProtocolV1ClientMessageStatus::StaleEpoch);

    result = handler.handle("{bad json", session, response);
    assert(result.status == NativeUiProtocolV1ClientMessageStatus::InvalidMessage);

    return 0;
}
