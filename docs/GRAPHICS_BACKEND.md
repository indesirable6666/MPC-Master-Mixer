# MPC Master Mixer — graphics backend preparation

Status: research and architecture only. No graphics context has been created on the MPC.

## Verified on the MPC Live II

- ARMv7 hard-float C++ executable runs on the target.
- `libEGL.so` and `libGLESv2.so` load; the earlier loader probe resolved the tested EGL and GLES entry points.
- `eglGetPlatformDisplayEXT` is exported and can also be obtained through `eglGetProcAddress`.
- `eglCreatePlatformWindowSurfaceEXT` can be obtained through `eglGetProcAddress`.
- The Mali library advertises `EGL_KHR_surfaceless_context` and `GL_OES_surfaceless_context`.
- `libgbm.so.1` is not installed.
- The running MPC process owns DRM master on `/dev/dri/card0`; its display plane is active.

## What this does not prove

- An EGL display or context can be initialized successfully.
- A surfaceless context can be created on this driver.
- A separate process can present pixels on the MPC touchscreen.
- A safe window/surface API exists for an independent application.
- Touch input can be acquired without interfering with MPC.
- Any graphics path can coexist with MPC's current DRM ownership.

The strings found in the Mali library are advertised names, not proof that every extension or operation works.

## Architecture decision

Keep the mixer UI independent from the graphics platform:

1. **Mixer model** — channel, master, EQ, pan, Aux, insert, mute/solo and meter state.
2. **UI interaction layer** — selection, gestures, scrolling and control editing.
3. **Renderer interface** — draws a frame from model state; no direct DRM or input-device access.
4. **Platform presentation adapter** — owns the eventual supported presentation path, selected only after a safe host/integration mechanism is verified.
5. **Control adapters** — MIDI or any future documented MPC control interface remain separate from rendering.

The existing browser prototype remains the reference implementation for layout and interaction. It must not be described as a live mixer.

## Candidate graphics path

EGL + OpenGL ES 2.x is the leading candidate for rendering because the libraries load and the previous probe resolved representative EGL/GLES symbols. A surfaceless EGL context is a possible off-screen rendering mode because the driver advertises `EGL_KHR_surfaceless_context`.

This is not yet a display solution. Surfaceless rendering alone does not put pixels on the touchscreen. GBM is not a viable assumed dependency on this image because `libgbm.so.1` is absent.

## Safety gates before any rendering test on hardware

- Do not stop MPC or take over `/dev/dri/card0`.
- Do not open DRM, initialize EGL, create a surface, write to `/dev/fb0`, or grab touchscreen input while MPC is running.
- Do not change display services, system files, startup scripts, ALSA routing or the root filesystem.
- First identify a documented/cooperative host integration path or a reversible test environment that does not compete with MPC for display ownership.
- Only then perform a minimal render test, with explicit user approval and a rollback plan.

## Next engineering task

Build a harmless symbol-resolution probe that uses `dlsym` and `eglGetProcAddress` to verify the actual GLES2 shader, buffer and draw entry points. It must not call EGL initialization, request a display, create a context, open DRM, access touch, or send MIDI/audio. This validates the function surface only; it does not validate rendering.
