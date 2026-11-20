# Guarded EGL offscreen pbuffer probe

This probe is a deliberate step beyond symbol lookup: it calls `eglGetDisplay`,
`eglInitialize`, selects an ES2 pbuffer configuration, creates a 64x64 pbuffer
and ES2 context, makes it current, and reads the GLES vendor/renderer/version.

## Safety boundary

- Build it in GitHub Actions only; building does not touch the MPC.
- Do not run it while MPC is open.
- The program requires the exact opt-in argument
  `--i-confirm-mpc-is-closed`.
- Before any EGL call it checks `/proc/*/comm` and
  `/sys/kernel/debug/dri/0/clients`; if MPC is detected, or either check
  cannot be completed, it stops.
- It creates no window and no EGL window surface; it uses only an offscreen
  pbuffer. It does not open DRM directly, write the framebuffer, or access
  touch, MIDI, audio, projects, or activation data.
- EGL initialization still enters the Mali driver and may access graphics
  resources. The guard is not a guarantee against driver bugs. Only run after
  MPC has been fully exited and only when the user has explicitly decided to
  perform this test.
- No system installation or root-filesystem changes are part of this test.

## Expected report

The program writes `mpc-egl-offscreen-report.txt` in its current directory.
A successful report proves that this driver can initialize EGL and create an
offscreen OpenGL ES 2 context. It does not prove that a separate application
can safely take over or present on the MPC touchscreen while MPC is running.

If EGL initialization or pbuffer creation fails, the report includes the EGL
error code. A failed test does not justify changing the system graphics stack.
