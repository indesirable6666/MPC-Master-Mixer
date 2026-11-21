#include "../src/audio/audio_engine.hpp"

#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdio>

namespace {
constexpr std::size_t kMaxFrames = 512;
constexpr float kSampleRate = 48000.0f;
constexpr int kWarmupBlocks = 200;
constexpr int kMeasuredBlocks = 1500;

void runBenchmark(std::size_t frameCount) {
    mpcmixer::AudioEngine engine;
    std::array<mpcmixer::OnePoleLowPassInsert, mpcmixer::AudioEngine::kTrackCount> lowPass{};
    std::array<mpcmixer::OnePoleHighPassInsert, mpcmixer::AudioEngine::kTrackCount> highPass{};
    std::array<std::array<float, kMaxFrames>, mpcmixer::AudioEngine::kTrackCount> inputStorage{};
    std::array<const float*, mpcmixer::AudioEngine::kTrackCount> inputs{};
    std::array<float, kMaxFrames> outputLeft{};
    std::array<float, kMaxFrames> outputRight{};
    std::array<std::array<float, kMaxFrames>, mpcmixer::AudioEngine::kAuxBusCount> auxLeftStorage{};
    std::array<std::array<float, kMaxFrames>, mpcmixer::AudioEngine::kAuxBusCount> auxRightStorage{};
    std::array<float*, mpcmixer::AudioEngine::kAuxBusCount> auxLeft{};
    std::array<float*, mpcmixer::AudioEngine::kAuxBusCount> auxRight{};

    const auto eq = mpcmixer::SixBandEq::defaultBandParameters();
    for (std::size_t track = 0; track < mpcmixer::AudioEngine::kTrackCount; ++track) {
        mpcmixer::AudioEngine::Track state;
        state.gain = 0.8f;
        state.pan = static_cast<float>(track % 5) * 0.4f - 0.8f;
        for (std::size_t bus = 0; bus < mpcmixer::AudioEngine::kAuxBusCount; ++bus)
            state.auxSend[bus] = 0.1f;
        if (!engine.setTrack(track, state) ||
            !engine.setTrackEq(track, kSampleRate, eq) ||
            !lowPass[track].configure(kSampleRate, 12000.0f) ||
            !highPass[track].configure(kSampleRate, 80.0f)) {
            std::fprintf(stderr, "Failed to configure benchmark track %zu\n", track);
            return;
        }
        lowPass[track].setEnabled(true);
        highPass[track].setEnabled(true);
        if (!engine.setTrackInsert(track, 0, &highPass[track]) ||
            !engine.setTrackInsert(track, 1, &lowPass[track])) {
            std::fprintf(stderr, "Failed to attach benchmark inserts\n");
            return;
        }
        inputs[track] = inputStorage[track].data();
        for (std::size_t frame = 0; frame < kMaxFrames; ++frame) {
            const float phase = static_cast<float>(frame) * 0.03125f +
                                static_cast<float>(track) * 0.17f;
            inputStorage[track][frame] = std::sin(phase) * 0.2f;
        }
    }
    for (std::size_t bus = 0; bus < mpcmixer::AudioEngine::kAuxBusCount; ++bus) {
        auxLeft[bus] = auxLeftStorage[bus].data();
        auxRight[bus] = auxRightStorage[bus].data();
    }

    auto processBlock = [&]() {
        engine.processWithAux(inputs.data(), outputLeft.data(), outputRight.data(),
                              auxLeft.data(), auxRight.data(), frameCount);
    };
    for (int i = 0; i < kWarmupBlocks; ++i) processBlock();

    volatile float sink = 0.0f;
    const auto start = std::chrono::steady_clock::now();
    for (int i = 0; i < kMeasuredBlocks; ++i) {
        processBlock();
        if ((i & 63) == 0) sink += outputLeft[frameCount / 2];
    }
    const auto finish = std::chrono::steady_clock::now();
    const double elapsedUs =
        std::chrono::duration<double, std::micro>(finish - start).count();
    const double averageUs = elapsedUs / kMeasuredBlocks;
    const double blockBudgetUs = 1.0e6 * frameCount / kSampleRate;
    const double realtimeLoad = 100.0 * averageUs / blockBudgetUs;
    const double throughput = static_cast<double>(frameCount) *
        kMeasuredBlocks / (elapsedUs / 1.0e6);

    std::printf("%4zu frames | %9.2f us/block | %8.2f%% of 48 kHz budget"
                " | %10.0f frames/s | checksum %.4f\n",
                frameCount, averageUs, realtimeLoad, throughput,
                static_cast<float>(sink));
}
} // namespace

int main() {
    std::puts("MPC Master Mixer — host-only DSP benchmark");
    std::puts("16 tracks, six-band EQ, two one-pole inserts, four Aux sends");
    std::puts("48 kHz float blocks; timings are diagnostic, not an MPC guarantee");
    runBenchmark(64);
    runBenchmark(128);
    runBenchmark(256);
    runBenchmark(512);
    return 0;
}
