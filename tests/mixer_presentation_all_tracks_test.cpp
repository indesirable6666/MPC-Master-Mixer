#include "../src/mixer/mixer_presentation_snapshot.hpp"

#include <cassert>
#include <cstddef>

int main() {
    mpcmixer::AudioEngineCommandBridge bridge;
    mpcmixer::MixerController controller(bridge);
    mpcmixer::MixerPresentationPublisher publisher(controller);

    // Give every strip a distinct value and Mute/Solo combination. This
    // catches off-by-one/index aliasing across the full 16-track surface.
    for (std::size_t track = 0; track < mpcmixer::AudioEngine::kTrackCount; ++track) {
        assert(controller.setTrackFader(track, static_cast<int>(60 + track)));
        assert(controller.setTrackPan(track, static_cast<int>(-80 + 10 * track)));
        assert(controller.setTrackMute(track, (track % 2) == 0));
        assert(controller.setTrackSolo(track, (track % 3) == 1));
    }
    assert(controller.setMasterFader(117));
    assert(controller.setMasterMute(true));

    const auto frame = publisher.publish();
    assert(frame.sequence == 1);
    assert(frame.tracks.size() == mpcmixer::AudioEngine::kTrackCount);
    for (std::size_t track = 0; track < mpcmixer::AudioEngine::kTrackCount; ++track) {
        assert(frame.tracks[track].faderValue == 60 + track);
        assert(frame.tracks[track].panPercent == -80 + 10 * static_cast<int>(track));
        assert(frame.tracks[track].mute == ((track % 2) == 0));
        assert(frame.tracks[track].solo == ((track % 3) == 1));
    }

    // Master controls are represented separately and cannot alias track 16.
    assert(frame.masterFaderValue == 117);
    assert(frame.masterMute);
    assert(frame.tracks[15].faderValue == 75);
    assert(!frame.tracks[15].mute);
    assert(!frame.tracks[15].solo);

    // A later edit to one strip must not mutate an already-published frame
    // or disturb any other strip's desired state.
    assert(controller.setTrackMute(7, false));
    assert(controller.setTrackSolo(7, true));
    const auto next = publisher.publish();
    assert(next.sequence == 2);
    assert(frame.tracks[7].mute == false); // track 7 was odd: initially false
    assert(frame.tracks[7].solo == true);  // track 7 % 3 == 1
    assert(next.tracks[7].mute == false);
    assert(next.tracks[7].solo == true);
    assert(next.masterFaderValue == frame.masterFaderValue);
    assert(next.masterMute == frame.masterMute);
    for (std::size_t track = 0; track < mpcmixer::AudioEngine::kTrackCount; ++track) {
        if (track == 7) continue;
        assert(next.tracks[track].mute == frame.tracks[track].mute);
        assert(next.tracks[track].solo == frame.tracks[track].solo);
    }
    return 0;
}
