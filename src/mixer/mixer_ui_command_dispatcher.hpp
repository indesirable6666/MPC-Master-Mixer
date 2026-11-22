#pragma once

#include "mixer_controller.hpp"

#include <cstdint>

namespace mpcmixer {

// Transport-neutral command envelope for a future native UI/IPC adapter.
// Values are expressed in the same units as the touchscreen prototype:
// faders 0..127, pan/balance -100..100, Aux sends 0..100.
enum class MixerUiCommandType : std::uint8_t {
    SetTrackFader,
    SetTrackPan,
    SetTrackMute,
    SetTrackSolo,
    SetTrackAuxSend,
    SetTrackEqBand,
    SetAuxReturn,
    SetMasterFader,
    SetMasterMute,
    SetAudioInputPairLinked,
    SetAudioInputPairStereoGain,
    SetAudioInputPairBalance,
    SetAudioInputPairStereoMute,
    SetAudioInputChannelGain,
    SetAudioInputChannelPan,
    SetAudioInputChannelMute,
    SetAudioInputChannelSolo,
    SetAudioInputChannelAuxSend,
    SetAudioInputChannelEqBand,
    SetInsertEnabled
};

struct MixerUiCommand {
    std::uint64_t sequence = 0;
    MixerUiCommandType type = MixerUiCommandType::SetMasterMute;
    std::size_t track = 0;
    std::size_t index = 0; // Aux bus, or EQ band depending on command type.
    int value = 0;
    int secondaryValue = 0; // Aux-return balance.
    bool flag = false; // Mute/Solo/Aux-return mute.
    float sampleRate = 48000.0f;
    SixBandEq::BandParameters eqBand{60.0f, 1.0f, 0.0f};
};

enum class MixerUiCommandStatus : std::uint8_t {
    Accepted,
    Invalid,
    OutOfOrder,
    QueueFull,
    Unsupported
};

struct MixerUiCommandResult {
    MixerUiCommandStatus status = MixerUiCommandStatus::Invalid;
    std::uint64_t sequence = 0;
    std::uint32_t audioTicket = 0;

    bool accepted() const noexcept {
        return status == MixerUiCommandStatus::Accepted;
    }
};

// Dispatch is called only from the same serialized producer/control thread as
// MixerController. It does not serialize bytes, create IPC, or touch devices.
// A rejected command does not consume its sequence, so QueueFull may be retried.
class MixerUiCommandDispatcher {
public:
    explicit MixerUiCommandDispatcher(MixerController& controller) noexcept
        : controller_(controller) {}

    MixerUiCommandResult dispatch(const MixerUiCommand& command) noexcept {
        if (command.sequence == 0 || command.sequence <= lastAcceptedSequence_)
            return {MixerUiCommandStatus::OutOfOrder, command.sequence};

        const std::uint32_t previousAudioTicket = controller_.lastAudioCommandTicket();
        bool accepted = false;
        switch (command.type) {
        case MixerUiCommandType::SetTrackFader:
            if (!validTrack(command.track) || command.value < 0 ||
                command.value > MixerController::kFaderMaximum)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setTrackFader(command.track, command.value);
            break;

        case MixerUiCommandType::SetTrackPan:
            if (!validTrack(command.track) ||
                command.value < MixerController::kPanMinimum ||
                command.value > MixerController::kPanMaximum)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setTrackPan(command.track, command.value);
            break;

        case MixerUiCommandType::SetTrackMute:
            if (!validTrack(command.track))
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setTrackMute(command.track, command.flag);
            break;

        case MixerUiCommandType::SetTrackSolo:
            if (!validTrack(command.track))
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setTrackSolo(command.track, command.flag);
            break;

        case MixerUiCommandType::SetTrackAuxSend:
            if (!validTrack(command.track) ||
                command.index >= AudioEngine::kAuxBusCount ||
                command.value < 0 || command.value > MixerController::kAuxMaximum)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setTrackAuxSend(
                command.track, command.index, command.value);
            break;

        case MixerUiCommandType::SetTrackEqBand: {
            if (!validTrack(command.track) ||
                command.index >= SixBandEq::kBandCount)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            auto bands = controller_.desiredTrackEq(command.track);
            bands[command.index] = command.eqBand;
            SixBandEq::PreparedConfiguration prepared;
            if (!SixBandEq::prepareConfiguration(
                    command.sampleRate, bands, prepared))
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setTrackEqBand(
                command.track, command.index, command.sampleRate, command.eqBand);
            break;
        }

        case MixerUiCommandType::SetAuxReturn: {
            if (command.index >= AudioEngine::kAuxBusCount ||
                command.value < 0 || command.value > MixerController::kFaderMaximum ||
                command.secondaryValue < MixerController::kPanMinimum ||
                command.secondaryValue > MixerController::kPanMaximum)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            AudioEngine::AuxReturn state;
            state.gain = static_cast<float>(command.value) / 100.0f;
            state.balance = static_cast<float>(command.secondaryValue) / 100.0f;
            state.mute = command.flag;
            accepted = controller_.setAuxReturn(command.index, state);
            break;
        }

        case MixerUiCommandType::SetMasterFader:
            if (command.value < 0 ||
                command.value > MixerController::kFaderMaximum)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setMasterFader(command.value);
            break;

        case MixerUiCommandType::SetMasterMute:
            accepted = controller_.setMasterMute(command.flag);
            break;

        case MixerUiCommandType::SetAudioInputPairLinked:
            accepted = controller_.setAudioInputPairLinked(command.flag);
            break;

        case MixerUiCommandType::SetAudioInputPairStereoGain:
            if (!controller_.desiredAudioInputPairLinked() || command.value < 0 ||
                command.value > MixerController::kFaderMaximum)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setAudioInputPairStereoGain(command.value);
            break;

        case MixerUiCommandType::SetAudioInputPairBalance:
            if (!controller_.desiredAudioInputPairLinked() ||
                command.value < MixerController::kPanMinimum ||
                command.value > MixerController::kPanMaximum)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setAudioInputPairBalance(command.value);
            break;

        case MixerUiCommandType::SetAudioInputPairStereoMute:
            if (!controller_.desiredAudioInputPairLinked())
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setAudioInputPairStereoMute(command.flag);
            break;

        case MixerUiCommandType::SetAudioInputChannelGain:
            if (!validAudioInputSide(command.track) || command.value < 0 ||
                command.value > MixerController::kFaderMaximum)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setAudioInputPairChannelGain(
                command.track, command.value);
            break;

        case MixerUiCommandType::SetAudioInputChannelPan:
            if (controller_.desiredAudioInputPairLinked() ||
                !validAudioInputSide(command.track) ||
                command.value < MixerController::kPanMinimum ||
                command.value > MixerController::kPanMaximum)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setAudioInputPairChannelPan(
                command.track, command.value);
            break;

        case MixerUiCommandType::SetAudioInputChannelMute:
            if (!validAudioInputSide(command.track))
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setAudioInputPairChannelMute(
                command.track, command.flag);
            break;

        case MixerUiCommandType::SetAudioInputChannelSolo:
            if (!validAudioInputSide(command.track))
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setAudioInputPairChannelSolo(
                command.track, command.flag);
            break;

        case MixerUiCommandType::SetAudioInputChannelAuxSend:
            if (!validAudioInputSide(command.track) ||
                command.index >= AudioEngine::kAuxBusCount ||
                command.value < 0 || command.value > MixerController::kAuxMaximum)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setAudioInputPairAuxSend(
                command.track, command.index,
                static_cast<float>(command.value) / 100.0f);
            break;

        case MixerUiCommandType::SetAudioInputChannelEqBand: {
            if (!validAudioInputSide(command.track) ||
                command.index >= SixBandEq::kBandCount)
                return {MixerUiCommandStatus::Invalid, command.sequence};
            auto bands = controller_.desiredStateSnapshot().audioInputEq[command.track];
            bands[command.index] = command.eqBand;
            SixBandEq::PreparedConfiguration prepared;
            if (!SixBandEq::prepareConfiguration(
                    command.sampleRate, bands, prepared))
                return {MixerUiCommandStatus::Invalid, command.sequence};
            accepted = controller_.setAudioInputPairEqBand(
                command.track, command.index, command.sampleRate, command.eqBand);
            break;
        }

        case MixerUiCommandType::SetInsertEnabled:
            // Effect construction and lifetime are native-host responsibilities.
            // A UI boolean must never be translated into an arbitrary effect pointer.
            return {MixerUiCommandStatus::Unsupported, command.sequence};

        default:
            return {MixerUiCommandStatus::Invalid, command.sequence};
        }

        if (!accepted)
            return {MixerUiCommandStatus::QueueFull, command.sequence};

        lastAcceptedSequence_ = command.sequence;
        const std::uint32_t currentAudioTicket = controller_.lastAudioCommandTicket();
        const std::uint32_t audioTicket =
            currentAudioTicket == previousAudioTicket ? 0u : currentAudioTicket;
        return {MixerUiCommandStatus::Accepted, command.sequence, audioTicket};
    }

    std::uint64_t lastAcceptedSequence() const noexcept {
        return lastAcceptedSequence_;
    }

private:
    static bool validTrack(std::size_t track) noexcept {
        return track < AudioEngine::kTrackCount;
    }

    static bool validAudioInputSide(std::size_t side) noexcept {
        return side < 2;
    }

    MixerController& controller_;
    std::uint64_t lastAcceptedSequence_ = 0;
};

} // namespace mpcmixer
