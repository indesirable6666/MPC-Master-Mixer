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

The repository now has a root `CMakeLists.txt`, but it is explicitly a host-side validation harness only. It builds the C++ test executables and optional benchmark; it does not define an application, renderer, window host, install target, or target-device toolchain. There is still no native application build definition, qmake/Meson project, `package.json`, application manifest, native UI entry point, or checked-in target SDK/sysroot.

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


## JUCE follow-up

The reported JUCE lock-file clue is assessed separately in [JUCE_FEASIBILITY.md](JUCE_FEASIBILITY.md). JUCE remains an unselected candidate until its exact version, independent SDK, target build configuration, license suitability, launch path, cooperative display integration, and touch-input path are established.


## Reproducible host test build

A root CMake project was added solely to make the existing C++ host tests reproducible outside CI. Configure with assertions enabled, build the test targets, then run CTest:

```sh
cmake -S . -B build/host -DCMAKE_BUILD_TYPE=Debug
cmake --build build/host --parallel
ctest --test-dir build/host --output-on-failure
```

The test programs use `assert`, so a Release configuration that defines `NDEBUG` would disable their checks. The CI validation therefore uses Debug for tests. The optional host benchmark is a separate non-gating target. This CMake project must not be mistaken for an MPC application or exact Buildroot cross-build.

## Official MPC Plugin SDK discovery (2026-11-23)

A new upstream project is now publicly available:
[inMusic's MPC Plugin SDK](https://github.com/inmusic-software/mpc-plugin-builder-sdk).
Its README states that the External Plugin Support feature requires MPC OS
3.10.1 or later. The supported 32-bit family explicitly includes MPC Live II;
the SDK builds VST2-compatible shared libraries loaded inside the MPC process.
Its UI is described as data/JSON skins rather than an independently hosted
window or application.

The SDK's build documentation gives an ARM32 Zig target of
`arm-linux-gnueabihf.2.31` with `-mcpu=cortex_a17`, and describes a GCC
alternative using ARMv7 hard-float flags. This is substantially more concrete
than the repository's generic Debian cross-compile profile, but it is a
plugin ABI/build route, not a general-purpose MPC application SDK or a
replacement for the native UI host/presentation path sought by this project.

**Decision for this branch:** do not integrate the SDK or change the build
system for the current MPC OS 2.15 target. The published SDK requires MPC OS
3.10.1+ and External Plugin Support, neither of which is established for the
target profile. Do not infer compatibility from the ARM32 CPU target or the
SDK's glibc compatibility note. Reassess only if the target firmware is
intentionally upgraded and the plugin-host feature is confirmed; even then,
this route would require redesigning the project as an in-process plugin and
would not provide a standalone mixer application or an independent screen
renderer.

Upstream references checked 2026-11-23:

- [SDK README: supported devices and firmware requirement](https://github.com/inmusic-software/mpc-plugin-builder-sdk/blob/main/README.md)
- [SDK build notes: ARM32 toolchain, ABI and UI model](https://github.com/inmusic-software/mpc-plugin-builder-sdk/blob/main/docs/building.md)
