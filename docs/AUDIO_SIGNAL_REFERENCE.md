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


## Aux send and return validation

`tests/audio_aux_send_return_test.cpp` exercises the four-bus Aux path using
single-frame synthetic buffers and the real `AudioEngine`. It verifies that
track sends are post-fader/post-pan but pre-Master, that Aux returns are summed
through return gain and balance into the Master path, and that return mute does
not alter the sends. It also checks that track mute and Solo suppress the
corresponding track sends, while Master mute silences the main output and
returns without suppressing pre-Master sends. The return signal is supplied as
an independent input to the mix call and is not routed back into the send
buffers, preventing an internal Aux feedback loop.

This is deterministic host-side DSP coverage only. It does not connect an
external effect, open an audio device, or validate physical MPC routing.


## Aux control bridge validation

`tests/audio_aux_control_bridge_test.cpp` exercises the producer-side
`MixerController` and the SPSC `AudioEngineCommandBridge` together. It queues
independent levels for all four sends on one track, updates two independent
returns, and verifies that the complete command sequence becomes visible at
the next audio block boundary. It then changes only selected sends and mutes
one return to verify that untouched buses retain their previous state.

The test also checks invalid return rejection and queue saturation. A failed
enqueue must not update the controller's desired return state; after the queue
drains, the same update can be published and is applied by the next block.
All buffers are synthetic and processed offline; this does not test device
I/O, external effects hardware, or MPC routing.


## Mixer controller rapid-edit validation

`tests/mixer_controller_state_sequence_test.cpp` submits rapid interleaved
fader, pan, mute, solo, and four-bus send edits on two tracks. It verifies that
each full-track command preserves the previously edited fields, that the
presentation snapshot reflects the latest desired state, and that the audio
consumer applies the queued snapshots in order at the next block boundary.

The same test fills the command queue and confirms that rejected fader, pan,
mute, solo, and send edits do not partially mutate the controller's desired
state or its presentation snapshot. Invalid ranges are also rejected without
consuming queue capacity. This is a deterministic host-side control/DSP test;
it does not exercise the touchscreen, device audio, or physical MPC.


## Mixer UI command dispatcher validation

`tests/mixer_ui_command_dispatcher_test.cpp` drives the transport-neutral UI command dispatcher through interleaved track, EQ, Aux return, Master, and stereo Audio In commands. It checks the published presentation snapshot, rejects zero/replayed/out-of-order sequence numbers and invalid command payloads without changing state, and confirms unsupported insert-toggle commands do not consume a sequence.

The queue-saturation case fills the SPSC bridge, verifies that a rejected command leaves both the controller's desired state and presentation snapshot unchanged, drains the queue at an offline block boundary, and retries the exact same sequence successfully. Increasing sequence gaps are accepted; only non-increasing values are rejected. This test is host/offline only and does not create IPC or communicate with an MPC.


## SPSC command bridge queue lifecycle stress test

`tests/audio_control_bridge_queue_lifecycle_test.cpp` repeatedly fills all
64 command slots, verifies that one additional enqueue is rejected without
changing the pending count, and drains the queue at a simulated audio block
boundary. It performs 256 complete ring rotations and checks that the final
FIFO command is the state applied by the audio engine on every batch, then
confirms the drained queue remains reusable. This is host/offline validation
of ring-slot reuse and backpressure only; it does not run concurrent producer
threads or access an audio device.
