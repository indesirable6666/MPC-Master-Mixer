#include "../src/audio/audio_engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>

namespace {
bool near(float actual, float expected, float tolerance = 1.0e-6f) {
    return std::fabs(actual - expected) <= tolerance;
}

struct Result {
    float left = 0.0f;
    float right = 0.0f;
    std::array<float, mpcmixer::AudioEngine::kAuxBusCount> sendLeft{};
    std::array<float, mpcmixer::AudioEngine::kAuxBusCount> sendRight{};
};

Result render(mpcmixer::AudioEngine& engine,
              const std::array<float, mpcmixer::AudioEngine::kTrackCount>& samples,
              const std::array<float, mpcmixer::AudioEngine::kAuxBusCount>& returnLeft = {},
              const std::array<float, mpcmixer::AudioEngine::kAuxBusCount>& returnRight = {}) {
    std::array<const float*, mpcmixer::AudioEngine::kTrackCount> inputs{};
    std::array<float, mpcmixer::AudioEngine::kTrackCount> trackSamples = samples;
    for (std::size_t i = 0; i < inputs.size(); ++i) {
        if (trackSamples[i] != 0.0f) inputs[i] = &trackSamples[i];
    }

    Result result;
    std::array<float*, mpcmixer::AudioEngine::kAuxBusCount> sendLeft{};
    std::array<float*, mpcmixer::AudioEngine::kAuxBusCount> sendRight{};
    std::array<const float*, mpcmixer::AudioEngine::kAuxBusCount> returnsL{};
    std::array<const float*, mpcmixer::AudioEngine::kAuxBusCount> returnsR{};
    for (std::size_t bus = 0; bus < mpcmixer::AudioEngine::kAuxBusCount; ++bus) {
        sendLeft[bus] = &result.sendLeft[bus];
        sendRight[bus] = &result.sendRight[bus];
        returnsL[bus] = &returnLeft[bus];
        returnsR[bus] = &returnRight[bus];
    }
    engine.processWithAuxAndReturns(inputs.data(), &result.left, &result.right,
                                    sendLeft.data(), sendRight.data(),
                                    returnsL.data(), returnsR.data(), 1);
    return result;
}
} // namespace

int main() {
    using Engine = mpcmixer::AudioEngine;
    Engine engine;
    Engine::Track track;
    track.gain = 0.5f;
    track.pan = -1.0f;
    track.auxSend[0] = 0.4f;
    track.auxSend[1] = 0.25f;
    assert(engine.setTrack(0, track));
    assert(engine.setMasterGain(0.25f));

    std::array<float, Engine::kTrackCount> samples{};
    samples[0] = 1.0f;

    // Sends are post-fader/post-pan but pre-Master: the Master fader scales
    // the direct mix, not the two Aux send taps.
    Result result = render(engine, samples);
    assert(near(result.left, 0.125f));
    assert(near(result.right, 0.0f));
    assert(near(result.sendLeft[0], 0.2f));
    assert(near(result.sendRight[0], 0.0f));
    assert(near(result.sendLeft[1], 0.125f));

    // Aux returns are summed into the Master path, with return gain/balance,
    // and do not feed back into any send bus.
    Engine::AuxReturn aux;
    aux.gain = 2.0f;
    aux.balance = 0.5f;
    assert(engine.setAuxReturn(0, aux));
    std::array<float, Engine::kAuxBusCount> returnLeft{};
    std::array<float, Engine::kAuxBusCount> returnRight{};
    returnLeft[0] = 0.3f;
    returnRight[0] = 0.4f;
    result = render(engine, samples, returnLeft, returnRight);
    assert(near(result.left, 0.2f));  // (0.5 + 0.3) * 0.25
    assert(near(result.right, 0.2f)); // (0.0 + 0.8) * 0.25
    assert(near(result.sendLeft[0], 0.2f));
    assert(near(result.sendRight[0], 0.0f));

    // Muting a return removes only that return; track sends remain unchanged.
    aux.mute = true;
    assert(engine.setAuxReturn(0, aux));
    result = render(engine, samples, returnLeft, returnRight);
    assert(near(result.left, 0.125f));
    assert(near(result.right, 0.0f));
    assert(near(result.sendLeft[0], 0.2f));

    // A muted track contributes neither to the main mix nor to its post-fader
    // sends. Another active return remains independent of that track mute.
    aux.mute = false;
    assert(engine.setAuxReturn(0, aux));
    track.mute = true;
    assert(engine.setTrack(0, track));
    result = render(engine, samples, returnLeft, returnRight);
    assert(near(result.left, 0.075f)); // return only: 0.3 * 0.25
    assert(near(result.right, 0.2f));  // return only: 0.8 * 0.25
    assert(near(result.sendLeft[0], 0.0f));
    assert(near(result.sendLeft[1], 0.0f));

    // Solo suppresses non-solo tracks and their sends; the soloed track's
    // sends remain active. Track 1 is deliberately configured as non-solo.
    Engine soloEngine;
    Engine::Track soloTrack;
    soloTrack.gain = 1.0f;
    soloTrack.solo = true;
    soloTrack.auxSend[0] = 0.5f;
    Engine::Track otherTrack;
    otherTrack.gain = 1.0f;
    otherTrack.auxSend[0] = 1.0f;
    assert(soloEngine.setTrack(0, soloTrack));
    assert(soloEngine.setTrack(1, otherTrack));
    std::array<float, Engine::kTrackCount> soloSamples{};
    soloSamples[0] = 0.4f;
    soloSamples[1] = 0.8f;
    result = render(soloEngine, soloSamples);
    assert(near(result.left, 0.4f * std::sqrt(0.5f)));
    assert(near(result.right, 0.4f * std::sqrt(0.5f)));
    assert(near(result.sendLeft[0], 0.2f * std::sqrt(0.5f)));
    assert(near(result.sendRight[0], 0.2f * std::sqrt(0.5f)));

    // Master mute silences the main output (including Aux returns) but leaves
    // the pre-Master Aux sends available to external effect/monitor paths.
    soloEngine.setMasterMute(true);
    result = render(soloEngine, soloSamples);
    assert(near(result.left, 0.0f));
    assert(near(result.right, 0.0f));
    assert(near(result.sendLeft[0], 0.2f * std::sqrt(0.5f)));
    assert(near(result.sendRight[0], 0.2f * std::sqrt(0.5f)));

    return 0;
}
