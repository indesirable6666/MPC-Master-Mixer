#pragma once

#include "audio_engine.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mpcmixer {

namespace detail {
// Advance the public acknowledgement ticket while reserving zero as invalid.
constexpr std::uint32_t nextAudioCommandTicket(std::uint32_t current) noexcept {
    return current == UINT32_MAX ? 1u : current + 1u;
}
} // namespace detail

// Single-producer/single-consumer command bridge between UI/control code and
// the real-time audio callback. The producer only enqueues value objects;
// the audio thread applies them immediately before processing a block.
// Do not call the producer API from multiple threads.
class AudioEngineCommandBridge {
public:
    static constexpr std::size_t kQueueCapacity = 64;

    AudioEngineCommandBridge() = default;
    AudioEngineCommandBridge(const AudioEngineCommandBridge&) = delete;
    AudioEngineCommandBridge& operator=(const AudioEngineCommandBridge&) = delete;

    bool enqueueTrack(std::size_t index, const AudioEngine::Track& state) noexcept {
        if (index >= AudioEngine::kTrackCount || !validTrack(state)) return false;
        Command command;
        command.type = CommandType::Track;
        command.index = index;
        command.track = state;
        return enqueue(command);
    }

    bool enqueueTrackEq(
        std::size_t index, float sampleRate,
        const std::array<SixBandEq::BandParameters, SixBandEq::kBandCount>& bands) noexcept {
        if (index >= AudioEngine::kTrackCount) return false;
        SixBandEq::PreparedConfiguration prepared;
        if (!SixBandEq::prepareConfiguration(sampleRate, bands, prepared))
            return false;
        Command command;
        command.type = CommandType::TrackEq;
        command.index = index;
        command.eq = prepared;
        return enqueue(command);
    }

    // The caller retains both old and new effect objects until the returned
    // nonzero ticket is acknowledged by hasApplied(). Destruction stays on control thread.
    bool enqueueTrackInsert(std::size_t trackIndex, std::size_t slot,
                            AudioInsert* effect, std::uint32_t& ticket) noexcept {
        if (trackIndex >= AudioEngine::kTrackCount ||
            slot >= InsertChain::kSlotCount) return false;
        Command command;
        command.type = CommandType::TrackInsert;
        command.index = trackIndex;
        command.slot = slot;
        command.effect = effect;
        return enqueue(command, &ticket);
    }

    // Control-thread query. Ticket zero is reserved as invalid. Sequence
    // comparisons are modulo 2^32 and remain unambiguous within half-range.
    // Once true, the audio thread has replaced the slot and will not
    // dereference the previous effect again.
    // Latest successfully enqueued command ticket, for the serialized producer thread.
    // This is distinct from the ring-buffer head index and skips zero on wrap.
    std::uint32_t lastEnqueuedTicket() const noexcept { return nextTicket_; }

    bool hasApplied(std::uint32_t ticket) const noexcept {
        if (ticket == 0) return false;
        const std::uint32_t applied =
            appliedSequence_.load(std::memory_order_acquire);
        return static_cast<std::uint32_t>(applied - ticket) < 0x80000000u;
    }

    bool enqueueAuxReturn(std::size_t index,
                          const AudioEngine::AuxReturn& state) noexcept {
        if (index >= AudioEngine::kAuxBusCount || !validReturn(state)) return false;
        Command command;
        command.type = CommandType::AuxReturn;
        command.index = index;
        command.auxReturn = state;
        return enqueue(command);
    }

    bool enqueueMasterGain(float gain) noexcept {
        if (!std::isfinite(gain) || gain < 0.0f ||
            gain > AudioEngine::kMaxFaderGain) return false;
        Command command;
        command.type = CommandType::MasterGain;
        command.value = gain;
        return enqueue(command);
    }

    bool enqueueMasterMute(bool muted) noexcept {
        Command command;
        command.type = CommandType::MasterMute;
        command.flag = muted;
        return enqueue(command);
    }

    bool enqueueAudioInputPairChannel(
        std::size_t side, const AudioInputPair::Channel& state) noexcept {
        if (!validInputPairChannel(side, state)) return false;
        Command command;
        command.type = CommandType::AudioInputPairChannel;
        command.index = side;
        command.inputPairChannel = state;
        return enqueue(command);
    }

    bool enqueueAudioInputPairStereoControls(
        const AudioInputPair::StereoControls& state) noexcept {
        if (!validInputPairStereo(state)) return false;
        Command command;
        command.type = CommandType::AudioInputPairStereoControls;
        command.inputPairStereo = state;
        return enqueue(command);
    }

    bool enqueueAudioInputPairLinked(bool linked) noexcept {
        Command command;
        command.type = CommandType::AudioInputPairLinked;
        command.flag = linked;
        return enqueue(command);
    }

    // Publish both channel states and the mode switch as one queue item.
    // Backpressure cannot leave a partially linked stereo pair.
    bool enqueueAudioInputPairState(
        bool linked, const AudioInputPair::StereoControls& stereo,
        const std::array<AudioInputPair::Channel, 2>& channels) noexcept {
        if (!validInputPairStereo(stereo) ||
            !validInputPairChannel(0, channels[0]) ||
            !validInputPairChannel(1, channels[1])) return false;
        Command command;
        command.type = CommandType::AudioInputPairState;
        command.flag = linked;
        command.inputPairStereo = stereo;
        command.inputPairChannels = channels;
        return enqueue(command);
    }

    // These methods are audio-thread-only. Pending commands are applied in
    // FIFO order at the block boundary, so no setter races process().
    void process(const float* const inputs[AudioEngine::kTrackCount],
                 float* outputLeft, float* outputRight,
                 std::size_t frameCount) noexcept {
        applyPending();
        engine_.process(inputs, outputLeft, outputRight, frameCount);
    }

    // Dedicated stereo Audio In mix-in. Pair controls are applied at the same
    // block boundary as track and Master commands; the shared Master stage is
    // also applied to the pair before it is added to the main stereo output.
    void processWithAudioInputPair(
        const float* const inputs[AudioEngine::kTrackCount],
        const float* inputLeft, const float* inputRight,
        float* outputLeft, float* outputRight,
        std::size_t frameCount) noexcept {
        applyPending();
        const bool pairSolo = audioInputPair_.hasSolo();
        engine_.processWithAuxAndReturns(
            inputs, outputLeft, outputRight, nullptr, nullptr, nullptr, nullptr,
            frameCount, pairSolo);
        audioInputPair_.processAndMix(
            inputLeft, inputRight, outputLeft, outputRight, frameCount,
            engine_.masterGain(), engine_.masterMute(),
            engine_.hasSoloedTrack());
    }

    void processWithAux(
        const float* const inputs[AudioEngine::kTrackCount],
        float* outputLeft, float* outputRight,
        float* const auxLeft[AudioEngine::kAuxBusCount],
        float* const auxRight[AudioEngine::kAuxBusCount],
        std::size_t frameCount) noexcept {
        applyPending();
        engine_.processWithAux(inputs, outputLeft, outputRight,
                               auxLeft, auxRight, frameCount);
    }

    void processWithAuxAndReturns(
        const float* const inputs[AudioEngine::kTrackCount],
        float* outputLeft, float* outputRight,
        float* const auxLeft[AudioEngine::kAuxBusCount],
        float* const auxRight[AudioEngine::kAuxBusCount],
        const float* const returnLeft[AudioEngine::kAuxBusCount],
        const float* const returnRight[AudioEngine::kAuxBusCount],
        std::size_t frameCount) noexcept {
        applyPending();
        engine_.processWithAuxAndReturns(inputs, outputLeft, outputRight,
                                         auxLeft, auxRight, returnLeft,
                                         returnRight, frameCount);
    }

    const AudioEngine& engineOnAudioThread() const noexcept { return engine_; }

    std::size_t pendingCountApprox() const noexcept {
        const std::uint32_t head = head_.load(std::memory_order_acquire);
        const std::uint32_t tail = tail_.load(std::memory_order_acquire);
        return static_cast<std::size_t>(head - tail);
    }

private:
    enum class CommandType : std::uint8_t {
        Track, TrackEq, TrackInsert, AuxReturn, MasterGain, MasterMute,
        AudioInputPairChannel, AudioInputPairStereoControls, AudioInputPairLinked,
        AudioInputPairState
    };

    struct Command {
        CommandType type = CommandType::MasterMute;
        std::size_t index = 0;
        std::uint32_t sequence = 0;
        std::size_t slot = 0;
        AudioInsert* effect = nullptr;
        AudioEngine::Track track{};
        SixBandEq::PreparedConfiguration eq{};
        AudioEngine::AuxReturn auxReturn{};
        AudioInputPair::Channel inputPairChannel{};
        std::array<AudioInputPair::Channel, 2> inputPairChannels{};
        AudioInputPair::StereoControls inputPairStereo{};
        float value = 0.0f;
        bool flag = false;
    };

    static bool validInputPairChannel(
        std::size_t side, const AudioInputPair::Channel& state) noexcept {
        return side < 2 && std::isfinite(state.gain) &&
            std::isfinite(state.pan) && state.gain >= 0.0f &&
            state.gain <= AudioEngine::kMaxFaderGain &&
            state.pan >= -1.0f && state.pan <= 1.0f;
    }

    static bool validInputPairStereo(
        const AudioInputPair::StereoControls& state) noexcept {
        return std::isfinite(state.gain) && std::isfinite(state.balance) &&
            state.gain >= 0.0f && state.gain <= AudioEngine::kMaxFaderGain &&
            state.balance >= -1.0f && state.balance <= 1.0f;
    }

    static bool validTrack(const AudioEngine::Track& state) noexcept {
        if (!std::isfinite(state.gain) || !std::isfinite(state.pan) ||
            state.gain < 0.0f || state.gain > AudioEngine::kMaxFaderGain ||
            state.pan < -1.0f || state.pan > 1.0f) return false;
        for (float send : state.auxSend) {
            if (!std::isfinite(send) || send < 0.0f || send > 1.0f) return false;
        }
        return true;
    }

    static bool validReturn(const AudioEngine::AuxReturn& state) noexcept {
        return std::isfinite(state.gain) && std::isfinite(state.balance) &&
            state.gain >= 0.0f && state.gain <= AudioEngine::kMaxFaderGain &&
            state.balance >= -1.0f && state.balance <= 1.0f;
    }

    bool enqueue(const Command& command, std::uint32_t* ticket = nullptr) noexcept {
        const std::uint32_t head = head_.load(std::memory_order_relaxed);
        const std::uint32_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= kQueueCapacity) return false;
        Command queued = command;
        // Tickets are independent of the ring index: skipping zero must not
        // cause a duplicate ticket on the command immediately after wrap.
        nextTicket_ = detail::nextAudioCommandTicket(nextTicket_);
        queued.sequence = nextTicket_;
        commands_[head % kQueueCapacity] = queued;
        head_.store(head + 1, std::memory_order_release);
        if (ticket != nullptr) *ticket = queued.sequence;
        return true;
    }

    bool dequeue(Command& command) noexcept {
        const std::uint32_t tail = tail_.load(std::memory_order_relaxed);
        const std::uint32_t head = head_.load(std::memory_order_acquire);
        if (tail == head) return false;
        command = commands_[tail % kQueueCapacity];
        tail_.store(tail + 1, std::memory_order_release);
        return true;
    }

    void applyPending() noexcept {
        Command command;
        while (dequeue(command)) {
            switch (command.type) {
            case CommandType::Track:
                engine_.setTrack(command.index, command.track);
                break;
            case CommandType::TrackEq:
                engine_.setTrackEqPrepared(command.index, command.eq);
                break;
            case CommandType::TrackInsert:
                engine_.setTrackInsert(command.index, command.slot, command.effect);
                break;
            case CommandType::AuxReturn:
                engine_.setAuxReturn(command.index, command.auxReturn);
                break;
            case CommandType::MasterGain:
                engine_.setMasterGain(command.value);
                break;
            case CommandType::MasterMute:
                engine_.setMasterMute(command.flag);
                break;
            case CommandType::AudioInputPairChannel:
                audioInputPair_.setChannel(command.index, command.inputPairChannel);
                break;
            case CommandType::AudioInputPairStereoControls:
                audioInputPair_.setStereoControls(command.inputPairStereo);
                break;
            case CommandType::AudioInputPairLinked:
                audioInputPair_.setLinked(command.flag);
                break;
            case CommandType::AudioInputPairState:
                audioInputPair_.setChannel(0, command.inputPairChannels[0]);
                audioInputPair_.setChannel(1, command.inputPairChannels[1]);
                audioInputPair_.setStereoControls(command.inputPairStereo);
                audioInputPair_.setLinked(command.flag);
                break;
            }
            appliedSequence_.store(command.sequence, std::memory_order_release);
        }
    }

    // Each queue slot is written by the producer before head_ is published,
    // then read by the consumer before tail_ is released.
    std::array<Command, kQueueCapacity> commands_{};
    alignas(64) std::atomic<std::uint32_t> head_{0};
    alignas(64) std::atomic<std::uint32_t> tail_{0};
    alignas(64) std::atomic<std::uint32_t> appliedSequence_{0};
    // Written/read only by the single serialized producer/control thread.
    std::uint32_t nextTicket_ = 0;
    AudioEngine engine_{};
    AudioInputPair audioInputPair_{};
};

} // namespace mpcmixer
