# MPC Live II — initial environment audit

These notes reflect read-only SSH inspection reported during project setup. Re-check all values on the target device before building or installing.

## Reported device environment

- Device: Akai MPC Live II
- MPC OS: 2.15 (as reported by the owner)
- Linux: Buildroot 2021.02.12
- Kernel: 5.15.118-inmusic-2023-06-21-p2-rt64
- Architecture: ARMv7 (armv7l)
- Main executable: `/usr/bin/MPC`
- Launcher: `/usr/bin/az01-launch-MPC`
- MPC system service: `inmusic-mpc.service`
- Hakai initialization script: `/usr/share/hakai/hakai-init`

## Observations

- The MPC executable contains a small number of strings mentioning QML. This is only a clue; it does not establish which Qt/QML version is installed or whether external QML applications can run.
- `chromium`, `chromium-browser`, `xdg-open`, `python3`, `file` and `ldd` were not found in the inspected shell environment.
- ALSA shared libraries are present under `/usr/lib`.
- Hakai's init script can execute a `hakai.sh` script from a specially labelled USB volume named `HAKAI`. This runs during initialization before the MPC application is launched. It is not evidence of native home-menu registration.
- The root filesystem was reported read-only and 100% full, with about 1.6 MB available. Do not write to it.
- Internal user storage is mounted at `/media/az01-internal`.

## Next read-only checks

1. Identify Qt libraries, QML modules and plugin directories.
2. Identify the display server and graphics environment.
3. Inspect ALSA devices and MIDI ports while MPC is running.
4. Determine whether MPC exposes documented OSC, MIDI Learn, control-surface or other external control interfaces.
5. Identify whether a supported application-launch or home-menu extension mechanism exists.

## Safety

Do not modify system services, the MPC executable, activation files, settings backups or the root filesystem during discovery. Back up user data before any future installation experiment.
