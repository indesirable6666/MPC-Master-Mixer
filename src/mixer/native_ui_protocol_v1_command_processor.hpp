#pragma once

#include "mixer_ui_command_dispatcher.hpp"
#include "native_ui_command_adapter.hpp"
#include "native_ui_protocol_v1_decoder.hpp"
#include "native_ui_protocol_v1_response_serializer.hpp"
#include "native_ui_session_coordinator.hpp"

#include <cstdint>
#include <string>

namespace mpcmixer {

enum class NativeUiProtocolV1CommandProcessStatus : std::uint8_t {
    Accepted,
    Rejected,
    InvalidMessage,
    StaleEpoch,
    NotReady,
    SerializationFailed
};

struct NativeUiProtocolV1CommandProcessResult {
    NativeUiProtocolV1CommandProcessStatus status =
        NativeUiProtocolV1CommandProcessStatus::InvalidMessage;
    MixerUiCommandResult commandResult{};
};

// Control-thread-only command ingress. It validates the v1 envelope, checks
// session readiness, dispatches through the bounded audio command tracker,
// and serializes the command_result response. No socket or UI ownership.
class NativeUiProtocolV1CommandProcessor {
public:
    NativeUiProtocolV1CommandProcessResult process(
        const std::string& payload,
        NativeUiSessionCoordinator& session,
        MixerUiCommandDispatcher& dispatcher,
        std::string& response) noexcept {
        NativeUiProtocolV1Command decoded;
        if (NativeUiProtocolV1Decoder::decode(payload, decoded) !=
            NativeUiProtocolV1DecodeStatus::Ok)
            return {NativeUiProtocolV1CommandProcessStatus::InvalidMessage, {}};

        const std::uint64_t activeEpoch = session.activeEpoch();
        if (activeEpoch == 0 || decoded.epoch != activeEpoch)
            return {NativeUiProtocolV1CommandProcessStatus::StaleEpoch, {}};
        if (!session.isReady(activeEpoch))
            return {NativeUiProtocolV1CommandProcessStatus::NotReady, {}};

        MixerUiCommand command;
        if (!NativeUiCommandAdapter::toCommand(decoded.command, command))
            return {NativeUiProtocolV1CommandProcessStatus::InvalidMessage, {}};

        const MixerUiCommandResult result =
            session.dispatch(activeEpoch, dispatcher, command);
        std::string candidate;
        if (!NativeUiProtocolV1ResponseSerializer::commandResult(
                activeEpoch, result, candidate))
            return {NativeUiProtocolV1CommandProcessStatus::SerializationFailed,
                    result};

        response.swap(candidate);
        return {result.accepted()
                    ? NativeUiProtocolV1CommandProcessStatus::Accepted
                    : NativeUiProtocolV1CommandProcessStatus::Rejected,
                result};
    }
};

} // namespace mpcmixer
