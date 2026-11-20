# MPC Master Mixer — project plan

## Goal

Develop a touchscreen mixer companion for the Akai MPC Live II running MPC OS 2.15. The visual direction is inspired by classic Cubase-era channel-strip layouts, without copying proprietary artwork or assets.

## Target mixer layout

- 16 channel strips visible at once, with horizontal scrolling for additional tracks.
- A dedicated Master strip fixed at the right edge.
- Six-band parametric EQ in the upper section of each channel strip.
- Insert effects above the pan/send section.
- Pan control and four independent auxiliary sends per track.
- Volume faders at the bottom.
- Mute, Solo and level meters.

## Development stages

1. **Hardware and runtime discovery** — identify the MPC's Qt/QML runtime, display environment, audio/MIDI interfaces and supported launch mechanisms.
2. **Interface prototype** — implement the mixer layout with touch-friendly controls and representative test channels.
3. **Control integration proof of concept** — determine whether supported MPC, MIDI, ALSA or other documented interfaces can read and control real track parameters.
4. **Audio feature validation** — prove EQ, inserts, sends, pan and fader changes affect the internal audio engine. Do not represent simulated controls as working audio controls.
5. **Packaging and launch** — build for the MPC's ARM/Linux environment and investigate a reversible home-menu entry.
6. **Testing and release** — test on MPC OS 2.15, document installation, rollback and known limitations.

## Software implementation status

The current branch contains a browser-only touch prototype and a portable,
allocation-free C++14 stereo DSP core with:
- Per-track six-band parametric EQ (frequency, Q and gain per band)
- Two insert slots per track, including an initial soft-clip effect
- Track gain, pan, Mute and Solo
- Four stereo Aux send buses and independently controlled stereo returns
- A linked/unlinked Audio In pair processor
- A device-independent capture interface whose default backend supplies
  explicit silence and never opens a device

Host CI tests the DSP behavior. The ARMv7 workflow also cross-compiles the
audio-engine tests for the audited hard-float instruction set and checks
runtime symbol versions. This is a cross-toolchain compatibility check, not
proof that the binary runs on the MPC or that the Debian sysroot exactly
matches its Buildroot image. See `docs/ARMV7_BUILD.md`.

The browser prototype still stores local demo state and is not yet connected
to the C++ engine. No live MPC audio, MIDI feedback, or device routing is
claimed.

Next software milestones: add more useful insert effects, establish host
block-processing performance, implement a verified UI-to-engine controller,
and investigate a matching Buildroot SDK/sysroot. Target audio capture, live
MIDI mappings, graphics/runtime selection, launcher integration and device
validation remain separate gated activities.

## Important constraints

- The MPC root filesystem was observed mounted read-only and almost full. Do not install files into the root filesystem.
- Prefer development and build artifacts on a computer; deploy only verified builds to user-accessible storage.
- Do not patch or replace the stock MPC executable or modify activation/licensing mechanisms.
- A graphical prototype is not an audio integration. Every live control must be validated against the actual MPC audio engine.
- Menu integration is an investigation item, not a promised capability.

## Acceptance criteria

- Touch operation is reliable at the MPC Live II screen resolution.
- Sixteen strips and the fixed Master strip are simultaneously visible as specified.
- Horizontal scrolling works without moving the Master strip.
- Each live control has a verified mapping to a real MPC parameter, or is clearly marked as unavailable.
- Installation and removal are documented and reversible.
