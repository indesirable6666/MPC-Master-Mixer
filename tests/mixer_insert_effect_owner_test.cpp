#include "../src/mixer/mixer_insert_effect_owner.hpp"
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace {
class CountingInsert final : public mpcmixer::AudioInsert {
public:
    explicit CountingInsert(int& count) noexcept : count_(count) {}
    ~CountingInsert() override { ++count_; }
    float processSample(float input) noexcept override { return input; }
private:
    int& count_;
};
void processBlock(mpcmixer::AudioEngineCommandBridge& bridge) {
    const float* inputs[mpcmixer::AudioEngine::kTrackCount]{};
    float left[1]{}, right[1]{};
    bridge.process(inputs, left, right, 1);
}
}

int main() {
    mpcmixer::AudioEngineCommandBridge bridge;
    mpcmixer::MixerController controller(bridge);
    mpcmixer::MixerInsertEffectOwner owner(controller);
    int destroyed = 0;
    std::uint32_t ticket = 0;

    auto first = std::unique_ptr<mpcmixer::AudioInsert>(new CountingInsert(destroyed));
    auto* firstPtr = first.get();
    assert(owner.publish(0, 0, first, ticket) && !first);
    assert(owner.current(0, 0) == nullptr && owner.hasPending(0, 0));
    assert(owner.collectApplied() == 0 && destroyed == 0);
    processBlock(bridge);
    assert(owner.collectApplied() == 1);
    assert(owner.current(0, 0) == firstPtr && destroyed == 0);

    auto second = std::unique_ptr<mpcmixer::AudioInsert>(new CountingInsert(destroyed));
    auto* secondPtr = second.get();
    assert(owner.publish(0, 0, second, ticket) && !second);
    assert(owner.current(0, 0) == firstPtr && destroyed == 0);
    assert(!owner.clear(0, 0, ticket));
    processBlock(bridge);
    assert(owner.collectApplied() == 1);
    assert(owner.current(0, 0) == secondPtr && destroyed == 1);

    auto independent = std::unique_ptr<mpcmixer::AudioInsert>(new CountingInsert(destroyed));
    assert(owner.publish(1, 1, independent, ticket));
    assert(owner.hasPending(1, 1) && !owner.hasPending(0, 0));
    processBlock(bridge);
    assert(owner.collectApplied() == 1);

    assert(owner.clear(0, 0, ticket));
    assert(owner.current(0, 0) == secondPtr && destroyed == 1);
    processBlock(bridge);
    assert(owner.collectApplied() == 1);
    assert(owner.current(0, 0) == nullptr && destroyed == 2);

    // Bulk shutdown clearing is incremental and covers track plus pair slots.
    mpcmixer::AudioEngineCommandBridge shutdownBridge;
    mpcmixer::MixerController shutdownController(shutdownBridge);
    mpcmixer::MixerInsertEffectOwner shutdownOwner(shutdownController);
    int shutdownDestroyed = 0;
    auto shutdownTrack = std::unique_ptr<mpcmixer::AudioInsert>(
        new CountingInsert(shutdownDestroyed));
    auto shutdownPair = std::unique_ptr<mpcmixer::AudioInsert>(
        new CountingInsert(shutdownDestroyed));
    assert(shutdownOwner.publish(2, 0, shutdownTrack, ticket));
    assert(shutdownOwner.publishAudioInputPair(1, 1, shutdownPair, ticket));
    processBlock(shutdownBridge);
    assert(shutdownOwner.collectApplied() == 2);
    assert(!shutdownOwner.allCleared());
    assert(!shutdownOwner.requestClearAll());
    assert(!shutdownOwner.allCleared());
    processBlock(shutdownBridge);
    assert(shutdownOwner.collectApplied() == 2);
    assert(shutdownOwner.allCleared());
    assert(shutdownDestroyed == 2);

    // A replacement still awaiting acknowledgement must keep bulk clear incomplete.
    mpcmixer::AudioEngineCommandBridge pendingBridge;
    mpcmixer::MixerController pendingController(pendingBridge);
    mpcmixer::MixerInsertEffectOwner pendingOwner(pendingController);
    int pendingDestroyed = 0;
    auto pendingFirst = std::unique_ptr<mpcmixer::AudioInsert>(
        new CountingInsert(pendingDestroyed));
    assert(pendingOwner.publish(0, 0, pendingFirst, ticket));
    processBlock(pendingBridge);
    assert(pendingOwner.collectApplied() == 1);
    auto pendingReplacement = std::unique_ptr<mpcmixer::AudioInsert>(
        new CountingInsert(pendingDestroyed));
    assert(pendingOwner.publish(0, 0, pendingReplacement, ticket));
    assert(!pendingOwner.requestClearAll());
    assert(!pendingOwner.allCleared());
    processBlock(pendingBridge);
    assert(pendingOwner.collectApplied() == 1);
    assert(pendingDestroyed == 1);
    assert(!pendingOwner.requestClearAll());
    processBlock(pendingBridge);
    assert(pendingOwner.collectApplied() == 1);
    assert(pendingOwner.allCleared());
    assert(pendingDestroyed == 2);

    // Queue pressure during bulk clearing is retryable after the consumer drains it.
    mpcmixer::AudioEngineCommandBridge retryBridge;
    mpcmixer::MixerController retryController(retryBridge);
    mpcmixer::MixerInsertEffectOwner retryOwner(retryController);
    int retryDestroyed = 0;
    auto retryInsert = std::unique_ptr<mpcmixer::AudioInsert>(
        new CountingInsert(retryDestroyed));
    assert(retryOwner.publish(0, 0, retryInsert, ticket));
    processBlock(retryBridge);
    assert(retryOwner.collectApplied() == 1);
    for (std::size_t i = 0; i < mpcmixer::AudioEngineCommandBridge::kQueueCapacity; ++i)
        assert(retryController.setMasterMute((i & 1u) != 0));
    assert(!retryOwner.requestClearAll());
    assert(!retryOwner.allCleared());
    processBlock(retryBridge);
    assert(retryOwner.collectApplied() == 0);
    assert(retryOwner.requestClearAll());
    processBlock(retryBridge);
    assert(retryOwner.collectApplied() == 1);
    assert(retryOwner.allCleared());
    assert(retryDestroyed == 1);

    mpcmixer::AudioEngineCommandBridge fullBridge;
    mpcmixer::MixerController fullController(fullBridge);
    mpcmixer::MixerInsertEffectOwner fullOwner(fullController);
    for (std::size_t i = 0; i < mpcmixer::AudioEngineCommandBridge::kQueueCapacity; ++i)
        assert(fullController.setMasterMute((i & 1u) != 0));
    auto rejected = std::unique_ptr<mpcmixer::AudioInsert>(new CountingInsert(destroyed));
    auto* rejectedPtr = rejected.get();
    assert(!fullOwner.publish(0, 0, rejected, ticket));
    assert(rejected.get() == rejectedPtr && !fullOwner.hasPending(0, 0));
    assert(!owner.publish(mpcmixer::AudioEngine::kTrackCount, 0, rejected, ticket));
    assert(rejected.get() == rejectedPtr);

    // Audio Input pair inserts use the same ownership/acknowledgement path.
    auto inputInsert = std::unique_ptr<mpcmixer::AudioInsert>(new CountingInsert(destroyed));
    auto* inputPtr = inputInsert.get();
    assert(owner.publishAudioInputPair(0, 1, inputInsert, ticket));
    assert(!inputInsert && owner.hasPendingAudioInputPair(0, 1));
    processBlock(bridge);
    assert(owner.collectApplied() == 1);
    assert(owner.currentAudioInputPair(0, 1) == inputPtr);
    assert(owner.clearAudioInputPair(0, 1, ticket));
    assert(owner.currentAudioInputPair(0, 1) == inputPtr);
    processBlock(bridge);
    assert(owner.collectApplied() == 1);
    assert(owner.currentAudioInputPair(0, 1) == nullptr);

    // Clear the other populated track slot too before owner teardown.
    assert(owner.clear(1, 1, ticket));
    processBlock(bridge);
    assert(owner.collectApplied() == 1);
    assert(owner.current(1, 1) == nullptr);
    return 0;
}
