# Host runtime discovery — repository audit

Date: 2026-11-21  
Branch: `feature/touch-mixer-ui`

## Result

No native application host/runtime or application build system is currently present in this repository. The branch contains:

- `prototype/index.html`: a standalone browser-based UI prototype.
- `src/audio/`, `src/mixer/`, and `src/midi/`: portable C++ headers for DSP, control, metering, and MIDI abstractions.
- `tests/`: host-side C++ test programs.
- `tools/arm-probe/`: ARMv7 loader and graphics capability probes.
- GitHub Actions workflows: CI compile/test commands, including ARMv7 cross-compilation.

The repository tree has no root or nested `CMakeLists.txt`, `Makefile`, Meson build definition, qmake project, `package.json`, application manifest, native UI entry point, or checked-in target SDK/sysroot.

The ARMv7 workflow compiles standalone probes and test executables with Debian Bullseye's hard-float cross compiler. It does not build a deployable mixer application and does not provide the MPC's exact Buildroot SDK/sysroot.

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

## Next repository milestone

Keep the framework-neutral sink contract. Add a concrete implementation only after the host runtime, target SDK, presentation path, and lifecycle have been established with evidence. Until then, continue improving the portable model and tests without inventing a platform integration.
