# Source code

The implementation language and UI framework are intentionally not fixed yet.

The device audit suggests QML may be relevant, but the installed Qt/QML runtime and build compatibility still need to be verified. Do not start a device-specific build until those checks are complete.

Planned components:

- `ui/` — mixer surface and channel-strip components.
- `audio/` — validated audio/control integration layer.
- `midi/` — optional MIDI bridge for supported mappings.
- `platform/` — MPC-specific launch and packaging integration.

Keep UI simulation separate from live audio-control code.