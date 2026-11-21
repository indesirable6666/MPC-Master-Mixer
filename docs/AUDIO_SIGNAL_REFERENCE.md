# Shared browser/native audio reference scenarios

The fixture `tests/fixtures/audio_signal_reference.csv` is the common numeric
contract for the browser demonstration and the native C++ mixer tests.

It currently covers five deterministic cases:

- centre pan with a 50% track gain;
- hard-left equal-power pan;
- track mute;
- 25% master gain;
- master mute.

The native test `tests/audio_signal_reference_test.cpp` feeds a fixed synthetic
mono block through `AudioDuplexEngineHost` and the offline backend, then checks
the actual stereo output and published Peak readings against the fixture.
CTest runs it from the repository root so the same CSV is loaded in local and
CI runs.

The browser interaction suite reads the same CSV and verifies the expected
equal-power gain and Master peak calculations. This is a reference-contract
check, not a browser audio render comparison: jsdom does not implement the Web
Audio DSP graph, and the browser demo still generates its own 220 Hz test tone.

The native track meter is intentionally pre-fader; the Master meter is
post-mix. The fixture currently compares output gain and Master peak, while
existing browser tests separately verify the analyser's Peak/RMS readout.

All tests are host/offline only. They do not open audio devices, use ALSA, send
MIDI, or communicate with an MPC. A target-device build and physical audio
validation remain separate steps requiring explicit authorization.
