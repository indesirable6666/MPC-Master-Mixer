# Host runtime discovery — repository and reported-device audit

Date: 2026-11-21  
Branch: `feature/touch-mixer-ui`

## Repository result

No native application host/runtime or application build system is currently present in this repository. The branch contains:

- `prototype/index.html`: a standalone browser-based UI prototype.
- `src/audio/`, `src/mixer/`, and `src/midi/`: portable C++ headers for DSP, control, metering, and MIDI abstractions.
- `tests/`: host-side C++ test programs.
- `tools/arm-probe/`: ARMv7 loader and graphics capability probes.
- GitHub Actions workflows: CI compile/test commands, including ARMv7 cross-compilation.

The repository tree has no root or nested `CMakeLists.txt`, `Makefile`, Meson build definition, qmake project, `package.json`, application manifest, native UI entry point, or checked-in target SDK/sysroot.

The ARMv7 workflow uses Debian Bullseye's hard-float cross compiler for standalone probes and test executables. Its latest recorded run (44873091575) completed successfully, including the ARMv7 meter-presentation adapter test artifact upload. This is a cross-compile check, not a deployable mixer application build and not the MPC's exact Buildroot SDK/sysroot.

## Reported target-runtime clues (not a confirmed development SDK)

The existing device-audit and UI-architecture notes record earlier read-only inspection findings:

- The MPC executable reportedly contains a few QML strings. This does not establish a usable Qt/QML runtime or SDK for third-party applications.
- Qt/QML, X11, and Wayland were not found in the inspected paths.
- A JUCE lock file was reportedly observed, suggesting JUCE may be involved in the stock MPC application. It does not establish an independent JUCE SDK, headers, libraries, version, license suitability, or supported external-app launch path.
- EGL/GLES libraries and loader symbols have been observed, but that alone does not establish a usable window/surface path or safe coexistence with the running MPC display owner.
- The prior device notes report that the running MPC owns DRM master. No independent presentation path has been demonstrated.

These are reported inspection notes, not fresh checks performed by this repository audit. Treat the Akai MPC Live II / MPC OS 2.15 / Buildroot 2021.02.12 / ARMv7 hard-float details as a build profile to reconfirm on the actual device before producing a deployable binary.

## Candidate assessment

| Candidate | Evidence | Missing proof | Current decision |
|---|---|---|---|
| Qt/QML | A few QML strings reportedly appear in the MPC executable | Installed version, headers, modules, plugins, external app support, target SDK | Do not select |
| JUCE | A JUCE lock file reportedly exists | Independent SDK, version, build config, license and supported display integration | Investigate only if evidence becomes available |
| EGL/GLES native renderer | Libraries and representative loader symbols were observed | Cooperative window/surface host, renderer context, input path and display ownership | Renderer candidate only; not a presentation solution |
| Browser/WebView | Standalone HTML prototype exists | Installed browser/WebView runtime and safe host integration | Not available as a verified target runtime |

## Consequence for the presentation adapter

`MeterPresentationAdapter` and `MeterPresentationSink` are intentionally framework-neutral. The adapter can produce copied 17-channel presentation frames when its host calls `tick()`, but this repository does not yet contain:

- A concrete native UI event loop that owns and schedules that call.
- A selected renderer or window/surface implementation.
- A verified touch-input integration.
- A concrete `MeterPresentationSink` implementation.
- An IPC transport connecting the C++ process to `prototype/index.html`.
- A native executable target or packaging/launch definition.

The browser prototype's JavaScript event seam is not an IPC channel. It must not be described as connected to the C++ engine.

## Required evidence before selecting a host

1. Identify the exact supported target SDK/sysroot and compiler/linker configuration for the MPC OS build.
2. Identify an independently usable UI framework/runtime and its exact target libraries/headers, rather than inferring one from strings or installed graphics libraries.
3. Establish a documented, cooperative presentation/launch path that does not take display ownership away from the running MPC application.
4. Establish the supported touch-input path and its ownership/sharing behavior.
5. Decide whether the native UI will host the mixer model directly or communicate with a separate process. Only then specify a transport and lifecycle.
6. Add a minimal host executable and fake/headless host tests before any hardware presentation experiment.

## Safety boundary

This repository audit is read-only and makes no claim about the current live state of the MPC beyond the separately recorded device-audit notes.

Do not patch or replace the MPC executable, alter services or startup scripts, modify ALSA routes, access the framebuffer, initialize EGL/DRM while MPC is running, or write to the root filesystem. Hardware display, touch, audio, or MIDI tests require separate explicit authorization. The current CI checks remain cross-compilation and host tests only.

## Next engineering milestone

Keep the framework-neutral sink contract. The next useful step is to collect documentary evidence for the target SDK and the JUCE/runtime clue (version, headers, libraries, build entry point, license and launch model). If no supported cooperative host can be established, continue with portable model/controller tests and the browser prototype rather than inventing a native integration. Add a concrete sink only after the host runtime, target SDK, presentation path, and lifecycle have been established with evidence.
