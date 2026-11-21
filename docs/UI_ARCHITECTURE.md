# MPC Master Mixer — UI architecture

## Goal

Build a touchscreen-first mixer interface for the Akai MPC Live II. The first milestone is a usable tactile UI prototype. Live audio-engine control is a separate milestone and must not be represented as working until verified on the device.

## Main layout

- 16 channel strips in a horizontally scrollable strip area.
- A dedicated Master strip remains fixed at the right edge while channel strips scroll.
- Controls are sized for finger operation on the MPC Live II touchscreen.
- Each channel strip is organized from top to bottom:
  1. Insert/effect area.
  2. Six-band parametric EQ area.
  3. Pan control.
  4. Four independent Aux Send controls.
  5. Mute and Solo controls.
  6. Volume fader and level meter.
- The Master strip has its own level meter and master-volume control.
- Peak/RMS meters include accessible percentage values, a decorative peak-hold marker, and a latched clip indicator; their meter semantics expose the current 0–100 value to assistive technology. The demo signal is synthetic and explicitly opt-in.
- The synthetic meter timer is created only while meter demo is enabled and is cleared immediately when disabled, avoiding idle background work.
- Touch controls use enlarged minimum hit areas for toolbar actions, pan and Aux sliders, Mute/Solo switches (40 px minimum height), and the Clip reset control (minimum 42 × 34 px). On narrow screens, the toolbar and footer compact while preserving the fixed Master strip and full-width channel touch targets. Long channel names truncate visually inside their strip with an ellipsis; the full name remains available in the title tooltip and selected-track footer.
- EQ and Aux controls use larger labels, clearer dB/percent readouts, taller touch targets, and accessible value announcements.
- All keyboard-focusable controls share a high-contrast `:focus-visible` outline so the active button or range control remains easy to locate when navigating without touch.
- The 16 channel strips are exposed as two explicit banks of eight; navigation announces the active bank and disables movement beyond either end. Bank navigation buttons have distinct accessible names and declare the strip viewport and live bank status they control. A `prefers-reduced-motion: reduce` media query disables smooth scrolling and nonessential channel/meter transitions.
- Track selection is highlighted and announced through an atomic polite status region; the announcement reads the selected strip's actual title text (including long names even when visually truncated). The bank indicator is also an atomic status region, and each meter panel is exposed as a named group for assistive technology. Keyboard users can move between adjacent strips with the left/right arrows, jump to the first/last strip with Home/End, or move by one eight-channel bank with PageUp/PageDown; crossing a bank boundary also updates the bank indicator and brings the corresponding bank into view.
- Insert buttons are visual placeholders with accessible pressed states; reset restores their illustrative defaults.
- Channel identity and selection must remain clear while scrolling.
- The UI should avoid requiring tiny controls or precision mouse interaction.

## UI-to-DSP parameter contract

The prototype's displayed ranges can be normalized without ambiguity when a
host-side controller is added:

- Track and Master fader: displayed 0–127 maps to linear gain 0.00–1.27
  (0 dB at 100; approximately +2.1 dB at 127).
- Mono pan: displayed -100–+100 maps to DSP -1.0–+1.0.
- Six EQ gain controls: displayed dB values map directly to each EQ band's
  `gainDb`; centre frequency and Q are separate DSP parameters.
- Aux send: displayed 0–100% maps to 0.0–1.0.
- Stereo balance: displayed -100–+100 maps to DSP -1.0–+1.0.
- Mute, Solo and insert enable state map to explicit booleans.

This is a parameter contract, not a live connection. The browser prototype
still stores local demo state and does not call the C++ engine.

## Interaction principles

- Touch operation is the primary interaction mode; no external MIDI controller is required.
- External MIDI surfaces should be an optional second control method.
- Provide a MIDI Learn workflow in the app only after the MIDI transport and MPC parameter behavior have been validated.
- Do not assume that every MPC parameter supports MIDI Learn. Validate volume, pan, each Aux Send, EQ parameters, inserts, mute, solo, and Master independently.
- Show clearly whether a control is in prototype/simulated mode or is confirmed to control the live MPC project.
- Avoid sending MIDI messages automatically on launch. Preserve existing ALSA connections and do not disconnect or reconfigure MPC ports without explicit approval.

## MIDI integration boundary

The device audit confirms ALSA sequencer ports exposed by the running MPC process and existing virtual MIDI endpoints. It also confirms that the MPC volume parameter can be assigned through MIDI Learn and that the assignment persists after saving and reopening a project, based on user testing.

This does **not** yet establish:
- Which MIDI message type, channel, and controller number MPC expects for each parameter.
- Whether parameter changes are sent back from MPC as MIDI feedback.
- Whether all channel strips can be addressed independently.
- Whether pan, Aux Sends, EQ, inserts, Mute/Solo, and Master are MIDI-Learn compatible.
- Whether a separate app can safely create and use an ALSA sequencer client alongside MPC.

Keep MIDI transport behind an adapter so it can be tested independently from the UI. Do not hard-code an unverified CC map. Add a parameter mapping only after a controlled test confirms it.

## Graphics/runtime constraints

Device inspection so far found:
- Linux Buildroot-based MPC environment.
- Direct framebuffer and DRM devices, including /dev/fb0 and /dev/dri/card0.
- DRM display connector card0-DSI-1 and /dev/rga2.
- EGL/GLES libraries are present on the filesystem, but were not observed in the selected MPC process mappings.
- No Qt/QML, X11, or Wayland installation was found in the inspected paths.
- MPC appears to use JUCE (a JUCE lock file is present), but an independent JUCE SDK/runtime has not been confirmed.

Do not select a UI framework or claim native app compatibility until the target build toolchain, graphics API, touch input path, and launch model are verified.

## Launch and safety constraints

- The user wants access from the MPC main menu, not the Effects menu.
- Native main-menu registration is unverified. A Hakai startup hook exists, but it is not proof of native menu integration.
- Do not patch or replace /usr/bin/MPC or other stock executables.
- Do not write to the read-only root filesystem.
- Do not change systemd services, startup scripts, MIDI routing, or device configuration without explicit user approval and a reversible plan.
- Prefer development and test files on user-accessible storage or in the project repository.

## Milestones

1. **UI specification and prototype:** implement the scrollable 16-strip layout and fixed Master using mock data only.
2. **Runtime feasibility:** identify a buildable graphics/UI stack and a safe launch method for this MPC OS.
3. **MIDI proof of concept:** create an isolated MIDI client and verify one explicitly learned volume parameter without disturbing existing connections.
4. **Parameter validation:** test pan, Aux Sends, EQ, inserts, Mute/Solo, and Master separately.
5. **State feedback:** determine whether the app can read current MPC parameter values; otherwise label controls as send-only and avoid pretending they stay synchronized.
6. **Packaging and launch:** investigate a reversible launcher and the requested main-menu access, without modifying stock MPC binaries.

## Current status

The UI direction is agreed: horizontal scrolling for the 16 channel strips, finger-friendly controls, and a Master strip fixed at the right. The first test mode will be touchscreen-only. External MIDI hardware may be connected later as an optional control surface.


## Prototype implementation status (2026-11)

The `prototype/index.html` page now contains a more complete touch-first mixer mock-up:

- 16 named channel strips in a horizontally scrollable bank.
- A fixed Master strip docked to the right.
- Six EQ controls, pan, four Aux Sends, insert placeholders, Mute/Solo and volume controls per strip.
- Local interaction for faders, EQ, pan, Aux, Mute/Solo and insert placeholder toggles; touching any channel action also selects and announces that channel.
- Bank navigation buttons and a reset action.
- Explicit offline/demo labeling; no network, MIDI, ALSA, audio-engine or device access.

All values and meters are illustrative UI state. The meter display is inactive by default; an explicit demo button animates synthetic values only. Peak Hold and Clip are visual demonstrations, not audio measurements. Insert buttons are accessible visual placeholders, not functioning effects. The prototype does not persist settings and does not communicate with the MPC.

This is a browser UI milestone only. It is not yet an ARM executable and must not be deployed as if it were an integrated MPC application.


## Entrées Audio In 1/2 — prototype de couplage stéréo

Les tranches 14 et 15 sont identifiées comme Audio In 1 et Audio In 2. Le bouton
`LINK AUDIO 1/2` couple visuellement ces deux tranches : le fader, les EQ,
les Aux Sends, Mute et Solo sont synchronisés depuis la tranche manipulée.
Les deux tranches restent affichées séparément avec les repères stéréo L/R.
Le panoramique mono est masqué pendant le couplage ; un réglage Balance
stéréo partagé devient disponible et agit sur les deux tranches. Le contrôle
de balance est indépendant des valeurs de pan mono conservées pour le mode
dissocié.

Ce couplage est une interaction locale de démonstration. Il ne sélectionne pas
les entrées physiques, ne capture aucun signal et ne modifie pas le routage
audio de la MPC. Le processeur C++ `AudioInputPair` traite des buffers fournis
par l'appelant, mais son raccordement à un backend audio matériel n'est pas
encore réalisé.

## Host runtime discovery

The repository-level runtime/build-system audit and the evidence required before choosing a native host are tracked in [HOST_RUNTIME_DISCOVERY.md](HOST_RUNTIME_DISCOVERY.md). The framework-neutral meter presentation sink remains the boundary until a supported host and target SDK are identified.


## Graphics probe evidence

The distinction between ARMv7 cross-compilation, symbol lookup, guarded offscreen EGL execution, and actual touchscreen presentation is documented in [GRAPHICS_PROBE_STATUS.md](GRAPHICS_PROBE_STATUS.md). CI compiles and archives the probes; it does not execute them on the MPC or validate an on-screen host.


## Mixer presentation snapshot contract

`src/mixer/mixer_presentation_snapshot.hpp` provides a framework-neutral,
UI-facing value snapshot for the 16 channel strips, four Aux returns and
Master. It copies fader/pan percentages, Mute/Solo, Aux send percentages,
six EQ band parameter sets, insert-slot presence flags, Aux return level and
balance, and Master level/mute.

The snapshot deliberately contains no `AudioInsert*` or engine pointers.
Insert slots are represented only as `hasInsert` booleans; this is a visual
presence hint, not an effect description or lifecycle handle.

`MixerPresentationPublisher` is called explicitly by the serialized
UI/control thread. It assigns a monotonically increasing 64-bit presentation
sequence and returns an immutable-by-convention value copy. It has no timer,
thread, lock, IPC, or delivery mechanism. A future host may serialize/copy
this value into its own transport, but must define ownership and synchronization
there.

**State meaning:** the snapshot reflects the controller's producer-side
*desired state*. It does not prove that queued commands have been consumed by
the audio callback, nor does it report actual MPC project state. A future
host must label or separately represent pending/applied/unknown state rather
than presenting this snapshot as hardware feedback.


## Native UI command envelope (host-side contract)

`src/mixer/mixer_ui_command_dispatcher.hpp` defines a transport-neutral
command envelope and a serialized dispatcher. The envelope carries a
monotonically increasing producer sequence, a command type, target indices,
and typed value fields. It currently covers track fader/pan/Mute/Solo,
Aux sends, one EQ band, Aux return level/balance/mute, Master fader/mute, and
linked/unlinked Audio In pair controls. Pair commands cover linked state,
stereo gain/balance/mute, and each side's independent gain/pan/mute/solo.
Channel-specific Audio In commands use the envelope's `track` field as side
0 (left) or 1 (right).

The dispatcher validates target indices and parameter ranges before calling
`MixerController`. Results distinguish accepted, invalid, out-of-order,
queue-full, and unsupported commands. A rejected command does not consume its
sequence, allowing a queue-full command to be retried after backpressure clears.
The host must preserve a single producer and must not dispatch concurrently
from multiple UI threads.

Fader values are raw 0–127 control values (linear gain = value / 100), not
percentages. Pan and Aux-return balance are -100–+100; Aux sends are 0–100.
The presentation snapshot therefore exposes `faderValue`,
`masterFaderValue`, and `levelValue` rather than misleading percent names;
these values can legitimately exceed 100.

Insert enable is intentionally reported as unsupported. The UI cannot supply
an arbitrary `AudioInsert*`: effect construction, ownership, and replacement
acknowledgement belong to a future native host. The command envelope is not
yet a byte-level serialization format, IPC transport, browser binding, or
proof of engine/device integration. The dispatcher itself is called on the
controller's serialized producer thread.


The desired-state and presentation snapshots also carry a value-only Audio In
pair section. It contains linked state, stereo gain/balance/mute and both
independent channel states. Like track state, it is a producer-side desired
copy: it is not proof that the audio callback has applied the command, and it
is not hardware feedback. The dedicated duplex pair path currently bypasses
per-track EQ, inserts and Aux sends; see
`docs/AUDIO_DUPLEX_ENGINE_HOST.md` for that explicit limitation.


## All-track presentation consistency

The `mixer_presentation_all_tracks_test` host test assigns distinct fader and pan values to all 16 strips and exercises independent Mute/Solo combinations. It verifies that each indexed strip is preserved in the presentation snapshot and that Master fader/mute remain separate from track 16. A second snapshot confirms that a later edit to one strip does not mutate the earlier value copy or alter neighboring strip states. This validates the controller's desired-state presentation contract, not applied engine state or physical MPC feedback.
