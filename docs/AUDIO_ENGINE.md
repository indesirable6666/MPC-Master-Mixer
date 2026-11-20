# Audio engine — first DSP milestone

## What exists

`src/audio/audio_engine.hpp` is a platform-independent, allocation-free C++14 mixing core. It accepts up to 16 caller-owned mono floating-point input buffers and writes a caller-owned stereo floating-point output block.

Implemented DSP building blocks:
- Six-band parametric EQ (`SixBandEq`): RBJ peaking biquads centred at 60, 180, 500, 1.5k, 5k and 12kHz, ±12dB, independent planar channel state

Implemented per track:
- Linear gain from 0.0 to 1.0
- Constant-power pan from hard left to hard right
- Mute and solo
- Four independent stereo Aux buses with per-track send levels
- Null input as silence

Implemented on the master bus:
- Master gain
- Master mute

The core ignores non-finite input samples and rejects invalid control values. The stereo float bus is not clipped; a future output stage must define limiting and sample-format conversion. Aux sends are post-fader and post-pan, pre-master; master gain/mute affect the main bus only. Aux output buffers are caller-owned and optional.

## Audio capture boundary

`src/audio/audio_backend.hpp` defines a device-independent `AudioCaptureBackend`
contract. Format preparation and shutdown belong on a control thread; the
capture callback uses caller-owned planar float buffers and must not allocate,
lock, log, or block. The included `SilentAudioCaptureBackend` is an explicit
offline default: it fills buffers with zero and reports `Silence`. It does not
open ALSA or any other device. This makes the boundary testable without
pretending that physical inputs are connected.

A future target backend must be implemented separately after the MPC's
supported capture API, channel mapping, buffer size, sample format, and routing
are independently established. It must not take ownership of or reconfigure
the MPC's existing audio device.

## What is deliberately not connected

This is a DSP block processor, not yet a live audio application. It does not:
- Integrate the standalone EQ stage into each channel's complete processing chain
- Implement insert effects
- Open ALSA, JACK, PulseAudio, or any audio device (the capture interface currently uses the silent backend only)
- Capture or play sound
- Read or write MPC audio routes
- Connect to MPC processes or modify device configuration
- Provide insert effects, resampling, or latency management

The current prototype controls are not wired to this engine. No real-time or hardware audio behavior is claimed.

## Next milestones

1. Establish block-processing performance on a host.
2. Verify the target's available audio APIs, device routing, and buffer constraints using a documented, read-only audit first.
3. Only after explicit hardware authorization, implement and test a target backend without changing MPC startup, audio routing, or system files.
4. Connect UI controls to the engine only after the backend and signal path are independently validated.

## Host tests

The `Audio engine tests` GitHub Actions workflow compiles with C++14, warnings-as-errors, and runs deterministic tests for summing, panning, gain, mute, solo, master controls, four stereo Aux buses, six-band EQ coefficient validation and signal processing, silent capture backend behavior, invalid parameters, and non-finite samples. These tests do not require audio hardware.


## Paire d'entrées Audio In

`AudioInputPair` représente deux entrées mono indépendantes (Audio In L et
Audio In R). Le mode non lié conserve deux voies mono avec gain, panoramique,
mute et solo indépendants. Le mode lié traite les deux buffers comme une seule
voie stéréo : le gain et le mute sont communs, le réglage Balance atténue le
côté opposé sans sommer les deux canaux ni détruire la séparation gauche/droite.

Le processeur est sans allocation et reçoit des buffers fournis par l'appelant.
Il ne capture aucun signal physique et n'ouvre aucun périphérique audio. La
connexion à l'entrée matérielle MPC et l'intégration dans le bus principal
restent des étapes séparées, à valider sur la cible avec autorisation explicite.
