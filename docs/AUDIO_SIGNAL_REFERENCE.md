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

## Insert frequency-response validation

`tests/audio_insert_frequency_response_test.cpp` measures steady-state RMS
ratios using synthetic sine waves at 48 kHz. It checks that the one-pole
low-pass preserves 100 Hz while attenuating 10 kHz, and that the high-pass
does the converse, with a 1 kHz cutoff. It also checks soft-clip symmetry and
bounded output under overload. The test is registered with CTest and run in
the host CI workflow.

These are broad response bounds for the implemented first-order filters, not
a claim that they are analogue-modelled EQ curves or a substitute for listening
tests. The six-band parametric EQ's centre-frequency response is separately
measured in `audio_engine_test`.

All tests are host/offline only. They do not open audio devices, use ALSA, send
MIDI, or communicate with an MPC. A target-device build and physical audio
validation remain separate steps requiring explicit authorization.
