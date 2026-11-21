# ARMv7 build notes

## Current CI cross-compile check

The `ARMv7 toolchain check` workflow uses Debian Bullseye's
`arm-linux-gnueabihf` hard-float cross compiler and the audited target flags:

```sh
-march=armv7-a -mfpu=neon-vfpv4 -mfloat-abi=hard
```

It compiles the C++14 audio-engine, duplex host, stereo Audio In pair, meter,
and shared signal-reference test programs as ARMv7 executables, then checks
ELF architecture, interpreter and runtime symbol versions. The workflow also
builds small graphics-loader capability probes. Test executables are uploaded
as CI artifacts for inspection; they are not executed on the MPC.

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

## Buildroot SDK handoff

The Debian Bullseye cross compiler above is a repeatable ARMv7 hard-float
compatibility check only. It is not the MPC's Buildroot SDK and must not be
used to infer the target's exact ABI or linked-library set.

A target-compatible cross-build is blocked until the matching Buildroot SDK
and sysroot are available. Before attempting it, record and verify:

- the exact Buildroot SDK release and target triplet;
- the SDK's environment-setup script or supplied CMake toolchain file;
- the target sysroot and its libc implementation/version;
- the compiler version, C++ standard-library ABI and exported GLIBC/GLIBCXX
  symbol versions;
- the SDK-provided CPU/FPU/float-ABI flags (do not assume the CI flags match);
- target headers and libraries for any later Qt, ALSA, EGL/GLES, GBM or DRM
  integration, including their versions and link dependencies.

When that SDK is available on a development workstation, prefer its own
environment setup and toolchain file rather than reconstructing flags by hand.
A generic CMake invocation is:

```sh
# Source the SDK's documented environment-setup script first, if provided.
cmake -S . -B build/mpc-buildroot \
  -DCMAKE_TOOLCHAIN_FILE=/path/from/sdk/toolchainfile.cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DMPC_BUILD_HOST_BENCHMARK=OFF
cmake --build build/mpc-buildroot --parallel
```

The toolchain file must identify the SDK compiler, target system and sysroot.
The current CMake project builds host-test executables, not a deployable MPC
application. A cross-build can establish compilation compatibility only;
do not run the resulting ARM binaries on the build host, copy them to the MPC,
or treat a successful link as runtime validation. Run tests only with a
separate, matching emulator or approved test harness. No such target SDK,
emulator or hardware test is currently part of this repository workflow.

No command in this procedure connects to an MPC, changes its filesystem or
services, configures ALSA/MIDI, or initializes EGL/DRM.
