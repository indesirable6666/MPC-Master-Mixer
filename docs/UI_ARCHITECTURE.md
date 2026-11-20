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
- Peak/RMS meters include a peak-hold marker and a latched clip indicator; the demo signal is synthetic and explicitly opt-in.
- The synthetic meter timer is created only while meter demo is enabled and is cleared immediately when disabled, avoiding idle background work.
- Touch controls use enlarged minimum hit areas for toolbar actions, pan/Aux sliders, and the Clip reset control. On narrow screens, the toolbar and footer compact while preserving the fixed Master strip and full-width channel touch targets. Long channel names truncate visually inside their strip with an ellipsis; the full name remains available in the title tooltip and selected-track footer.
- EQ and Aux controls use larger labels, clearer dB/percent readouts, taller touch targets, and accessible value announcements.
- The 16 channel strips are exposed as two explicit banks of eight; navigation announces the active bank and disables movement beyond either end.
- Track selection is highlighted and announced through a polite live region; the announcement reads the selected strip's actual title text (including long names even when visually truncated). Keyboard users can move between adjacent strips with the left/right arrows, or jump to the first/last strip with Home/End.
- Insert buttons are visual placeholders with accessible pressed states; reset restores their illustrative defaults.
- Channel identity and selection must remain clear while scrolling.
- The UI should avoid requiring tiny controls or precision mouse interaction.

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
