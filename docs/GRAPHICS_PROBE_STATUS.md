# Graphics probe status and execution boundary

Date: 2026-11-21

## CI-confirmed facts

Workflow run [44873091575](https://github.com/indesirable6666/MPC-Master-Mixer/actions/runs/44873091575) completed successfully. Its ARMv7 job compiled and uploaded these standalone probes:

- `graphics-load-probe.cpp`: loads EGL/GLES libraries and resolves a small set of loader symbols.
- `egl-client-extension-probe.cpp`: inspects client extension metadata.
- `egl-gbm-symbol-probe.cpp`: resolves EGL platform/GBM-related symbols and checks whether `libgbm.so.1` can be loaded.
- `gles2-symbol-probe.cpp`: resolves shader, program, buffer, vertex-attribute, draw, viewport, clear and diagnostic entry-point addresses.
- `egl-offscreen-pbuffer-probe.cpp`: a guarded active EGL/GLES test binary.

The workflow compiles these programs for ARMv7 hard-float and uploads the binaries. It does not execute them on an MPC device. Successful cross-compilation proves source/toolchain compatibility only; it does not prove target library availability or runtime behavior.

## Important distinction between probes

### Symbol-only probes

The graphics loader, EGL/GBM symbol, and GLES2 symbol probes call `dlopen`, `dlsym`, and (where applicable) `eglGetProcAddress`. They do not initialize EGL, request a display, create a context, call GL rendering functions, open DRM, or access touch.

The GLES2 probe resolves a candidate shader-to-draw function surface, but does not invoke any of those functions. It is not a rendering test.

### Guarded pbuffer probe

`egl-offscreen-pbuffer-probe.cpp` is materially different: if its guards pass, it calls EGL initialization, creates an ES2 context and a 64×64 offscreen pbuffer, clears it, and reads back one pixel.

It requires the exact opt-in argument `--i-confirm-mpc-is-closed`, checks `/proc` for the MPC process, and checks the DRM client list. It fails closed if either inspection cannot be performed. These checks are defensive safeguards, not a substitute for an approved test plan or proof that every possible display conflict is absent.

**This active probe has only been cross-compiled in CI. It has not been run on the MPC. Do not run it while MPC is active.** Its guard does not grant permission to stop MPC, take over DRM, or perform a hardware test.

## Current graphics conclusion

- EGL/GLES libraries and symbols have been reported from prior device inspection; this is not revalidated by the CI run.
- ARMv7 probe binaries compile and are archived.
- No EGL context creation, GLES draw, pbuffer readback, window surface, touch input, or on-screen presentation has been demonstrated by this repository's CI.
- An offscreen pbuffer would validate only offscreen rendering. It would not solve the independent-app presentation or cooperative display-host problem.
- No graphics backend or UI framework should be selected from symbol availability alone.

## Next safe engineering step

Continue repository-only work: keep the rendering/presentation boundary abstract, record target SDK and runtime evidence when available, and maintain host tests. Any active graphics test on the MPC requires separate explicit authorization and a verified safe state. Do not stop or modify MPC services, acquire DRM master, write framebuffer memory, or alter the root filesystem.
