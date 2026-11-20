# Audio engine — first DSP milestone

## What exists

`src/audio/audio_engine.hpp` is a platform-independent, allocation-free C++14 mixing core. It accepts up to 16 caller-owned mono floating-point input buffers and writes a caller-owned stereo floating-point output block.

Implemented per track:
- Linear gain from 0.0 to 1.0
- Constant-power pan from hard left to hard right
- Mute and solo
- Null input as silence

Implemented on the master bus:
- Master gain
- Master mute

The core ignores non-finite input samples and rejects invalid control values. The stereo float bus is not clipped; a future output stage must define limiting and sample-format conversion.

## What is deliberately not connected

This is a DSP block processor, not yet a live audio application. It does not:
- Open ALSA, JACK, PulseAudio, or any audio device
- Capture or play sound
- Read or write MPC audio routes
- Connect to MPC processes or modify device configuration
- Provide EQ, inserts, Aux buses, resampling, or latency management

The current prototype controls are not wired to this engine. No real-time or hardware audio behavior is claimed.

## Next milestones

1. Expand DSP tests and establish block-processing performance on a host.
2. Define an audio I/O backend separately from the DSP core.
3. Verify the target's available audio APIs, device routing, and buffer constraints using a documented, read-only audit first.
4. Only after explicit hardware authorization, implement and test a target backend without changing MPC startup, audio routing, or system files.
5. Connect UI controls to the engine only after the backend and signal path are independently validated.

## Host tests

The `Audio engine tests` GitHub Actions workflow compiles with C++14, warnings-as-errors, and runs deterministic tests for summing, panning, gain, mute, solo, master controls, invalid parameters, and non-finite samples. These tests do not require audio hardware.
