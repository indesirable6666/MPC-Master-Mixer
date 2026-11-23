#pragma once

#include "mixer_ui_command_dispatcher.hpp"

#include <cstddef>
#include <cstdint>
#include <string>

namespace mpcmixer {

// Typed result of decoding one transport message. The transport/parser is
// responsible for setting presence flags only after checking JSON types.
struct NativeUiCommandMessage {
    std::uint64_t sequence = 0;
    std::string type;
    bool hasTrack = false;
    std::size_t track = 0;
    bool hasIndex = false;
    std::size_t index = 0;
    bool hasValue = false;
    int value = 0;
    bool hasFlag = false;
    bool flag = false;
};

class NativeUiCommandAdapter {
public:
    static MixerUiCommandResult dispatch(
        const NativeUiCommandMessage& message,
        MixerUiCommandDispatcher& dispatcher) noexcept {
        MixerUiCommand command;
        if (!toCommand(message, command))
            return {MixerUiCommandStatus::Invalid, message.sequence};
        return dispatcher.dispatch(command);
    }

    static bool toCommand(
        const NativeUiCommandMessage& message,
        MixerUiCommand& command) noexcept {
        command.sequence = message.sequence;

        if (message.type == "SetTrackFader") {
            if (!message.hasTrack || !message.hasValue ||
                message.track >= AudioEngine::kTrackCount ||
                message.value < 0 || message.value > MixerController::kFaderMaximum)
                return false;
            command.type = MixerUiCommandType::SetTrackFader;
            command.track = message.track;
            command.value = message.value;
            return true;
        }
        if (message.type == "SetTrackPan") {
            if (!message.hasTrack || !message.hasValue ||
                message.track >= AudioEngine::kTrackCount ||
                message.value < MixerController::kPanMinimum ||
                message.value > MixerController::kPanMaximum)
                return false;
            command.type = MixerUiCommandType::SetTrackPan;
            command.track = message.track;
            command.value = message.value;
            return true;
        }
        if (message.type == "SetTrackAuxSend") {
            if (!message.hasTrack || !message.hasIndex || !message.hasValue ||
                message.track >= AudioEngine::kTrackCount ||
                message.index >= AudioEngine::kAuxBusCount ||
                message.value < 0 || message.value > MixerController::kAuxMaximum)
                return false;
            command.type = MixerUiCommandType::SetTrackAuxSend;
            command.track = message.track;
            command.index = message.index;
            command.value = message.value;
            return true;
        }
        if (message.type == "SetTrackMute" || message.type == "SetTrackSolo") {
            if (!message.hasTrack || !message.hasFlag ||
                message.track >= AudioEngine::kTrackCount)
                return false;
            command.type = message.type == "SetTrackMute"
                ? MixerUiCommandType::SetTrackMute
                : MixerUiCommandType::SetTrackSolo;
            command.track = message.track;
            command.flag = message.flag;
            return true;
        }
        if (message.type == "SetMasterFader") {
            if (!message.hasValue || message.value < 0 ||
                message.value > MixerController::kFaderMaximum)
                return false;
            command.type = MixerUiCommandType::SetMasterFader;
            command.value = message.value;
            return true;
        }
        if (message.type == "SetMasterMute") {
            if (!message.hasFlag)
                return false;
            command.type = MixerUiCommandType::SetMasterMute;
            command.flag = message.flag;
            return true;
        }
        return false;
    }
};

} // namespace mpcmixer
