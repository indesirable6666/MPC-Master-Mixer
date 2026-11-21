# JUCE feasibility assessment

Date: 2026-11-21
Status: investigation only — JUCE is not selected.

## Evidence currently available

The repository's earlier device-audit notes report that a JUCE-related lock file was observed in the MPC environment. This is an indirect clue only. The repository does not contain JUCE source, JUCE modules, a JUCE project, a JUCE build definition, or a JUCE target SDK/sysroot.

No verified evidence currently establishes:
- The JUCE version used by the stock MPC application.
- That JUCE headers or libraries are installed and intended for third-party use.
- That the stock application's JUCE components are dynamically linkable by an independent process.
- That a supported JUCE application entry point or launch mechanism exists.
- That JUCE can create a cooperative window/surface without competing with the running MPC display owner.
- That the matching ARMv7 Buildroot compiler, sysroot, C++ runtime and linker flags are available.
- That the relevant JUCE modules and dependencies can be built for the target.
- That the applicable JUCE license terms permit the intended distribution and use.

## Minimum evidence needed before choosing JUCE

1. Read-only confirmation of the exact JUCE version and where the lock-file clue originates.
2. An independently usable JUCE source tree or SDK with matching headers and modules.
3. A reproducible target build configuration using the exact target toolchain/sysroot, not merely the Debian ARM cross-compiler used by CI.
4. A documented application lifecycle and supported launch path.
5. A documented cooperative presentation mechanism that does not stop MPC, take DRM master, write the framebuffer, or interfere with the stock display.
6. A separately verified touch-input path and explicit ownership/lifecycle rules.
7. A license review for the exact JUCE version and intended deployment.

## Current architecture decision

Keep the mixer model, presentation-frame adapter, renderer boundary and platform presentation boundary framework-neutral. Do not add JUCE headers, modules, linker flags, binary dependencies or a JUCE-specific sink until the evidence above is available.

A JUCE renderer would solve only rendering. It would not, by itself, solve application launch, display ownership, touch input, or communication with the audio engine.

## Safe next step

Continue repository-side work and host CI. Any further device inspection must remain read-only and within the existing authorization boundary. No system services, stock MPC files, activation data, audio routing, DRM, framebuffer, touch devices or root filesystem should be changed or taken over.
