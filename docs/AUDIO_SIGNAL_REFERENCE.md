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


## Six-band EQ frequency-response validation

`tests/audio_eq_frequency_response_test.cpp` sends steady-state sine waves
through each of the six parametric bands independently at that band's centre
frequency. Both +6 dB boost and -6 dB cut are checked against their expected
linear amplitude ratios with small floating-point tolerances.

The same test also processes a sine through the actual `AudioEngine` track
path with a +6 dB EQ band, an enabled 0.5x Gain insert, a 0.8x track fader,
and centre equal-power pan. Its left output RMS is checked against the
combined expected gain. This covers the EQ-before-insert ordering and the
subsequent fader/pan stages together.

These tests are deterministic host-side DSP checks. They do not render through
a browser, open a device, or establish the sound of a physical MPC output.


## Insert chain ordering and bypass validation

`tests/audio_insert_chain_test.cpp` drives the real `AudioEngine` track path
with two active inserts. A nonlinear soft clip followed by a gain trim verifies
that slot order is preserved. The test then bypasses each slot independently,
checks the both-bypassed unity path, and verifies that an empty slot is
transparent and an out-of-range slot is rejected. The test runs in both CTest
and the native host CI workflow.


## Combined EQ and insert-chain validation

`tests/audio_eq_insert_chain_test.cpp` measures the combined `AudioEngine`
track path with a +6 dB boost at 500 Hz, a 3 kHz one-pole low-pass in insert
slot 0, a 0.5x Gain insert in slot 1, a 0.8x track fader, and centre pan.
The enabled low-pass result is checked against its analytical one-pole
magnitude multiplied by the EQ, insert, fader, and pan gains. A second fresh
run bypasses only the low-pass and verifies that the frequency-dependent loss
disappears while the EQ and Gain insert remain active. This validates the
combined signal path and the bypass state without opening an audio device.
