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

    // Clear the other populated slot too; no engine slot may retain a pointer
    // after the owner is destroyed.
    assert(owner.clear(1, 1, ticket));
    processBlock(bridge);
    assert(owner.collectApplied() == 1);
    assert(owner.current(1, 1) == nullptr);
    return 0;
}
