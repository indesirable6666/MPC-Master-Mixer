# ARMv7 build notes

## Current CI cross-compile check

The `ARMv7 toolchain check` workflow uses Debian Bullseye's
`arm-linux-gnueabihf` hard-float cross compiler and the audited target flags:

```sh
-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
```

It compiles the C++14 audio-engine test program as an ARMv7 executable, then
checks ELF architecture, interpreter and runtime symbol versions. The workflow
also builds small graphics-loader capability probes. The audio test executable
is uploaded as a CI artifact for inspection; it is not executed on the MPC.

## What this does and does not prove

A successful cross-compile confirms that the current portable DSP and silent
capture interface compile for this ARMv7 hard-float target configuration. It
does not prove binary compatibility with every library in the MPC Buildroot
image, real-time performance, availability of ALSA capture, graphics
compatibility, or safe coexistence with the stock MPC application.

The Debian cross compiler is a compatibility check, not a replacement for the
exact Buildroot SDK/sysroot. Before producing a deployable application, obtain
and use the matching target headers, libraries, linker configuration and
runtime ABI. Do not deploy the CI test binary as an application.

## Current audio boundary

The portable core has no ALSA, JACK, Qt, EGL, DRM, or MPC process dependency.
The default capture backend returns explicit silence. No device access,
configuration changes, or live audio validation are performed by CI.
