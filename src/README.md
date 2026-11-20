# Source code

The portable audio core is C++14 and is kept independent of the MPC runtime.
The browser prototype remains a separate, offline-only UI.

Current audio components:

- `audio/audio_engine.hpp` — 16-track stereo summing, track/master gain,
  pan, Mute/Solo, six-band EQ, two insert slots per track, four stereo Aux
  sends and returns, and the linked Audio In pair processor.
- `audio/six_band_eq.hpp` — allocation-free six-band parametric EQ with
  per-band frequency, Q and gain controls.
- `audio/insert_effects.hpp` — non-owning two-slot insert chain and initial
  soft-clip processor.
- `audio/audio_backend.hpp` — device-independent capture interface and an
  explicit silent offline backend.
- `mixer/meter_model.hpp` — peak/RMS/clip state model.
- `midi/midi_adapter.hpp` — opt-in MIDI adapter interfaces with no assumed
  MPC CC mappings.

DSP configuration methods belong on a control thread and must not race audio
block processing. The core does not open ALSA or any device and does not read
or write MPC parameters. The default capture backend only returns silence.

The target UI framework and live device backend remain undecided until the
Buildroot SDK, graphics/input runtime, audio API, and safe launch model are
verified. See `docs/ARMV7_BUILD.md` and `docs/AUDIO_ENGINE.md`.
