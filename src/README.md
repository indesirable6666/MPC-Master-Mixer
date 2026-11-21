# Source code

The portable audio core is C++14 and is kept independent of the MPC runtime.
The browser prototype remains a separate, offline-only UI.

Current audio components:

- `audio/audio_engine.hpp` — 16-track stereo summing, track/master gain,
  pan, Mute/Solo, six-band EQ, two insert slots per track, four stereo Aux
  sends and returns, and the standalone linked Audio In pair processor.
- `audio/audio_control_bridge.hpp` — fixed-capacity SPSC control queue that
  applies track, prepared EQ, Aux and Master updates at audio block boundaries.
- `audio/audio_duplex_backend.hpp` — device-independent full-duplex callback
  contract and deterministic offline callback driver.
- `audio/audio_duplex_engine_host.hpp` — explicit input-channel-to-track
  routing and connection from duplex buffers through the mixer to stereo output.
- `audio/audio_backend.hpp` — legacy capture-only interface and explicit
  silent offline backend.
- `audio/six_band_eq.hpp` — allocation-free six-band parametric EQ with
  per-band frequency, Q and gain controls.
- `audio/insert_effects.hpp` — non-owning two-slot insert chain and initial
  soft-clip processor.
- `mixer/meter_model.hpp` — peak/RMS/clip state model.
- `midi/midi_adapter.hpp` — opt-in MIDI adapter interfaces with no assumed
  MPC CC mappings.

DSP configuration methods belong on a control thread and must not race audio
block processing. The command bridge is single-producer/single-consumer; the
duplex host's per-input routes are atomic. The host requires an explicit
channel-to-track mapping and defaults all inputs to unmapped.

The offline duplex backend only forwards synthetic/caller-owned buffers. No
ALSA, JACK, MPC device capture, or hardware playback backend is implemented.
The stereo float output still needs a target output stage for limiting and
sample-format conversion.

The browser prototype is not wired to the C++ bridge; a native host/controller
adapter is still required. See `docs/AUDIO_CONTROL_BRIDGE.md`,
`docs/AUDIO_DUPLEX_BACKEND.md`, and
`docs/AUDIO_DUPLEX_ENGINE_HOST.md`.

The target UI framework and live device backend remain undecided until the
Buildroot SDK, graphics/input runtime, audio API, and safe launch model are
verified. See `docs/ARMV7_BUILD.md` and `docs/AUDIO_ENGINE.md`.
