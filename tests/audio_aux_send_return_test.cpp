#include "../src/audio/audio_engine.hpp"

#include <array>
#include <cassert>
#include <cmath>
#include <cstddef>
#include <limits>

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

    // The engine's direct control API independently validates Aux Return
    // values, so callers outside the command bridge cannot publish bad state.
    Engine validationEngine;
    const Engine::AuxReturn originalReturn = validationEngine.auxReturn(0);
    Engine::AuxReturn invalidReturnState;
    invalidReturnState.gain = std::numeric_limits<float>::quiet_NaN();
    assert(!validationEngine.setAuxReturn(0, invalidReturnState));
    invalidReturnState.gain = std::numeric_limits<float>::infinity();
    assert(!validationEngine.setAuxReturn(0, invalidReturnState));
    invalidReturnState.gain = -0.01f;
    assert(!validationEngine.setAuxReturn(0, invalidReturnState));
    invalidReturnState.gain = Engine::kMaxFaderGain + 0.01f;
    assert(!validationEngine.setAuxReturn(0, invalidReturnState));
    invalidReturnState.gain = 1.0f;
    invalidReturnState.balance = std::numeric_limits<float>::quiet_NaN();
    assert(!validationEngine.setAuxReturn(0, invalidReturnState));
    invalidReturnState.balance = std::numeric_limits<float>::infinity();
    assert(!validationEngine.setAuxReturn(0, invalidReturnState));
    invalidReturnState.balance = -1.01f;
    assert(!validationEngine.setAuxReturn(0, invalidReturnState));
    invalidReturnState.balance = 1.01f;
    assert(!validationEngine.setAuxReturn(0, invalidReturnState));
    invalidReturnState.balance = 0.0f;
    assert(!validationEngine.setAuxReturn(Engine::kAuxBusCount, invalidReturnState));
    assert(near(validationEngine.auxReturn(0).gain, originalReturn.gain));
    assert(near(validationEngine.auxReturn(0).balance, originalReturn.balance));
    assert(validationEngine.auxReturn(0).mute == originalReturn.mute);

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
    aux.gain = 1.0f;
    aux.balance = 0.5f;
    assert(engine.setAuxReturn(0, aux));
    std::array<float, Engine::kAuxBusCount> returnLeft{};
    std::array<float, Engine::kAuxBusCount> returnRight{};
    returnLeft[0] = 0.3f;
    returnRight[0] = 0.4f;
    result = render(engine, samples, returnLeft, returnRight);
    assert(near(result.left, 0.1625f)); // (0.5 + 0.15) * 0.25
    assert(near(result.right, 0.1f));   // 0.4 * 0.25
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
    assert(near(result.left, 0.0375f)); // return only: 0.15 * 0.25
    assert(near(result.right, 0.1f));    // return only: 0.4 * 0.25
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

    // Non-finite return samples and absent input planes are treated as
    // silence independently, rather than contaminating the stereo mix.
    Engine returnRobustnessEngine;
    std::array<const float*, Engine::kTrackCount> silentInputs{};
    std::array<float*, Engine::kAuxBusCount> noSends{};
    std::array<const float*, Engine::kAuxBusCount> robustReturnLeft{};
    std::array<const float*, Engine::kAuxBusCount> robustReturnRight{};
    const float invalidReturnSample = std::numeric_limits<float>::quiet_NaN();
    const float validReturn = 0.4f;
    robustReturnLeft[0] = &invalidReturnSample;
    robustReturnRight[0] = &validReturn;
    float robustLeft = 9.0f;
    float robustRight = 9.0f;
    returnRobustnessEngine.processWithAuxAndReturns(
        silentInputs.data(), &robustLeft, &robustRight,
        noSends.data(), noSends.data(),
        robustReturnLeft.data(), robustReturnRight.data(), 1);
    assert(near(robustLeft, 0.0f));
    assert(near(robustRight, 0.4f));

    // A missing return plane is independently silent; the other side remains
    // valid. Master mute still silences the complete main output.
    robustReturnLeft[0] = nullptr;
    robustReturnRight[0] = &validReturn;
    returnRobustnessEngine.processWithAuxAndReturns(
        silentInputs.data(), &robustLeft, &robustRight,
        noSends.data(), noSends.data(),
        robustReturnLeft.data(), robustReturnRight.data(), 1);
    assert(near(robustLeft, 0.0f));
    assert(near(robustRight, 0.4f));
    returnRobustnessEngine.setMasterMute(true);
    returnRobustnessEngine.processWithAuxAndReturns(
        silentInputs.data(), &robustLeft, &robustRight,
        noSends.data(), noSends.data(),
        robustReturnLeft.data(), robustReturnRight.data(), 1);
    assert(near(robustLeft, 0.0f));
    assert(near(robustRight, 0.0f));

    return 0;
}
