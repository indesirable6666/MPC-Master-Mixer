#pragma once

#include "audio_engine.hpp"

#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace mpcmixer {

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

    // These methods are audio-thread-only. Pending commands are applied in
    // FIFO order at the block boundary, so no setter races process().
    void process(const float* const inputs[AudioEngine::kTrackCount],
                 float* outputLeft, float* outputRight,
                 std::size_t frameCount) noexcept {
        applyPending();
        engine_.process(inputs, outputLeft, outputRight, frameCount);
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
    enum class CommandType : std::uint8_t { Track, AuxReturn, MasterGain, MasterMute };

    struct Command {
        CommandType type = CommandType::MasterMute;
        std::size_t index = 0;
        AudioEngine::Track track{};
        AudioEngine::AuxReturn auxReturn{};
        float value = 0.0f;
        bool flag = false;
    };

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

    bool enqueue(const Command& command) noexcept {
        const std::uint32_t head = head_.load(std::memory_order_relaxed);
        const std::uint32_t tail = tail_.load(std::memory_order_acquire);
        if (head - tail >= kQueueCapacity) return false;
        commands_[head % kQueueCapacity] = command;
        head_.store(head + 1, std::memory_order_release);
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
            case CommandType::AuxReturn:
                engine_.setAuxReturn(command.index, command.auxReturn);
                break;
            case CommandType::MasterGain:
                engine_.setMasterGain(command.value);
                break;
            case CommandType::MasterMute:
                engine_.setMasterMute(command.flag);
                break;
            }
        }
    }

    // Each queue slot is written by the producer before head_ is published,
    // then read by the consumer before tail_ is released.
    std::array<Command, kQueueCapacity> commands_{};
    alignas(64) std::atomic<std::uint32_t> head_{0};
    alignas(64) std::atomic<std::uint32_t> tail_{0};
    AudioEngine engine_{};
};

} // namespace mpcmixer
