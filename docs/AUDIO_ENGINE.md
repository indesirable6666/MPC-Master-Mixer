# Audio engine — first DSP milestone

## What exists

`src/audio/audio_engine.hpp` is a platform-independent, allocation-free C++14 mixing core. It accepts up to 16 caller-owned mono floating-point input buffers and writes a caller-owned stereo floating-point output block.

Implemented per track:
- Six-band parametric EQ (`SixBandEq`): six RBJ peaking biquads with independently configurable centre frequency (20Hz to Nyquist), Q (0.1–10) and gain (±12dB); default centres are 60, 180, 500, 1.5k, 5k and 12kHz, with independent planar channel state
- Linear gain from 0.0 to 1.27 (+2.1 dB), matching the prototype's 0–127 fader range
- Constant-power pan from hard left to hard right
- Mute and solo
- Two non-owning insert slots per track, with tested utility gain, cubic soft-clip and stateful one-pole low-pass and high-pass effects
- Four independent stereo Aux buses with per-track send levels and stereo returns (gain, balance, mute)
- Null input as silence

Implemented on the master bus:
- Master gain (0.0–1.27, +2.1 dB maximum)
- Master mute

The core ignores non-finite input samples and rejects invalid control values. The stereo float bus is not clipped; a future output stage must define limiting and sample-format conversion. Aux sends are post-fader and post-pan, pre-master; stereo returns are summed into the main bus before master gain/mute and are never fed back into sends. Aux output and return buffers are caller-owned and optional.

## Current signal flow

For each mono track the host DSP path is:

```text
input buffer -> six-band EQ -> insert slot 1 -> insert slot 2
            -> track gain -> constant-power pan -> main stereo sum
                                               -> four post-fader Aux sends

four stereo Aux return buffers -> return gain/balance/mute -> main stereo sum
main stereo sum -> Master gain/mute -> stereo float output
```

Mute and Solo are evaluated before the track contributes to the main or Aux
sends. Aux returns are added only to the main bus, so they cannot recursively
feed the send buses. The Master control affects the main output but not the
pre-master Aux sends. The float output can exceed full scale and still needs a
separate limiter/format-conversion stage before any hardware output.

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

## Control-thread to audio-thread updates

The direct engine setters must not race the audio callback. The new
`src/audio/audio_control_bridge.hpp` provides a fixed-capacity SPSC command
queue so a single host/UI control thread can publish track, prepared EQ, Aux
return and Master changes for application at the next audio block boundary.
EQ coefficients are calculated on the producer thread and copied/reset by the
audio thread without trigonometric work. Queue-full conditions are reported
to the producer. Insert-slot replacements can also be queued as non-owning
pointers; the producer must retain old and new effect objects until the
corresponding ticket is acknowledged with `hasApplied()`, then retire the
old object off the audio thread. The browser prototype
is not yet wired to a native host/controller. See
`docs/AUDIO_CONTROL_BRIDGE.md` for the threading contract.

## Duplex engine host

`src/audio/audio_duplex_engine_host.hpp` connects the duplex callback contract
to the command bridge and mixer. It accepts up to two explicit input channels,
maps each channel to a selected mono mixer track, and produces stereo output.
All inputs begin unmapped; no physical MPC/ALSA channel ordering is assumed.
The routing entries are atomic so the host can change an individual mapping
without racing the audio callback. Duplicate mappings within one block are
treated as invalid and produce silence.

The host lifecycle (`prepare`, `start`, `stop`, `shutdown`) belongs to the
control thread. The backend must stop callbacks before host destruction. A host
cannot be prepared twice without shutdown. The deterministic offline backend
tests this complete software route using synthetic planar-float buffers.

This does not create a physical audio connection. There is still no target
ALSA/JACK/MPC backend, device discovery, hardware channel map, limiter, or
sample-format conversion. The browser prototype also remains disconnected
from the native host/controller.

## What is deliberately not connected

This is a DSP block processor, not yet a live audio application. It does not:
- Add further insert effects (utility gain, cubic soft clip and one-pole low-pass are implemented)
- Open ALSA, JACK, PulseAudio, or any audio device (the capture interface currently uses the silent backend only)
- Capture or play sound
- Read or write MPC audio routes
- Connect to MPC processes or modify device configuration
- Provide resampling or latency management

The browser prototype controls are not yet wired to this engine. Host-side DSP tests verify that configured per-track EQ and inserts run before gain/pan and feed both the main and Aux buses. No real-time or hardware audio behavior is claimed.

## Next milestones

1. Establish block-processing performance on a host.
2. Verify the target's available audio APIs, device routing, and buffer constraints using a documented, read-only audit first.
3. Only after explicit hardware authorization, implement and test a target backend without changing MPC startup, audio routing, or system files.
4. Connect UI controls to the engine only after the backend and signal path are independently validated.

## Host performance benchmark\n\n`tools/audio-engine-benchmark.cpp` is a standalone, host-only diagnostic. It\nprocesses 16 synthetic tracks through six-band EQ, two one-pole inserts per\ntrack, and four Aux sends at 48 kHz. It reports average block time, the\npercentage of each real-time block budget consumed, and frame throughput for\n64, 128, 256 and 512-frame blocks. All buffers and effects are prepared\nbefore timing; the measured loop performs only block processing. Build with\n`g++ -O3 -DNDEBUG -std=c++14 -Wall -Wextra -Werror -pedantic\ntools/audio-engine-benchmark.cpp -o audio-engine-benchmark` and run\n`./audio-engine-benchmark`. Results depend on the host CPU, compiler and load;\nthey are not a target-MPC performance claim or a real-time certification.\n\n## Host tests

The `Audio engine tests` GitHub Actions workflow compiles with C++14, warnings-as-errors, and runs deterministic tests for summing, panning, gain, mute, solo, master controls, four stereo Aux sends and returns, six-band EQ frequency/Q/gain validation and signal processing, two-slot insert chains, soft clipping, and one-pole filter response, silent capture backend behavior, invalid parameters, and non-finite samples. These tests do not require audio hardware.


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


## Full-duplex backend contract

`src/audio/audio_duplex_backend.hpp` defines a future host boundary for
simultaneous planar-float capture and playback. The format carries sample rate,
input/output channel counts and maximum block size. Lifecycle operations belong
to the control thread; the realtime callback contract forbids allocation,
locks, logging and blocking. `OfflineAudioDuplexBackend` is a deterministic
callback test driver only: it forwards caller-owned buffers and never opens a
device. Its tests establish callback plumbing and validation, not hardware
timing, latency or MPC routing. The earlier `AudioCaptureBackend` remains a
capture-only legacy interface; actual target audio integration is still absent.
\n\n## Block-level meter measurement\n\n`src/audio/audio_block_meter.hpp` provides a stateless, allocation-free helper to measure peak and RMS from a contiguous mono float block. It rejects null/empty blocks and any non-finite sample without modifying the caller's previous reading, and clamps display amplitudes to [0,1]. The helper does not capture audio, publish across threads, or connect the browser prototype to the DSP engine; callers must provide valid buffers and transfer readings through an explicitly synchronized path.\n