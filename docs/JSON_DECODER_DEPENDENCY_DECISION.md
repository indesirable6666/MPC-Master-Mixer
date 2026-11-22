# JSON decoder dependency decision

## Decision

Use **nlohmann/json v3.12.0** for the first native UI JSON decoder, subject
to the ARMv7 cross-compile gate described below. Do not use the moving
`develop` branch. Pin the release artifact or an exact upstream commit and
record its checksum in the dependency manifest when the dependency is vendored.

Upstream release: https://github.com/nlohmann/json/releases/tag/v3.12.0

The upstream release page publishes the SHA-256 of its single-header
`json.hpp` artifact:

```
aaf127c04cb31c406e5b04a63f1ae89369fccde6d8fa7cdda1ed4f32dfc5de63
```

## Why this choice

- The project is C++14; nlohmann/json requires C++11 or newer and documents
  support for GCC 4.8 and later. The current ARM workflow uses the Debian
  Bullseye `arm-linux-gnueabihf-g++` toolchain and already compiles project
  tests as C++14.
- The library can be embedded as a single `json.hpp` header, avoiding a new
  linked runtime library or target-device package installation.
- Its SAX interface reports every object key occurrence and distinguishes
  signed integers, unsigned integers, and floating-point values. That lets
  the decoder reject duplicate top-level keys and reject fractions/exponent
  notation without converting wire integers through floating point.
- The v3.12.0 release is a dated, versioned upstream release with a published
  artifact checksum. Dependency provenance can therefore be pinned and
  verified.

## Alternatives considered

| Library | Advantages | Reasons not selected |
|---|---|---|
| RapidJSON 1.1.0 | Header-only, SAX and DOM APIs, SAX exposes `Int64`, `Uint64`, `Double`, and each `Key` event | Its latest tagged release remains v1.1.0; no compelling advantage over the newer nlohmann release for this small control protocol |
| Boost.JSON | C++11, streaming parser, optional header-only mode | Broader Boost component surface; its documented object insertion keeps only the last duplicate key, so strict duplicate rejection still requires a SAX/event-level decoder |

This is a compatibility/design choice, not a performance claim. No binary
size, compile-time, or runtime benchmark has been run on the target.

## Decoder implementation constraints

Implement the decoder with nlohmann/json's SAX interface rather than parsing
to a DOM and inspecting the resulting object:

1. Enforce the transport frame-size limit before parsing.
2. Accept exactly one top-level object and scalar values only; reject nested
   objects and arrays.
3. Track the five permitted keys in a fixed enum/bit mask. Reject a repeated
   key as soon as its SAX `key` event arrives; reject every unknown key.
4. Accept `sequence` only from the unsigned-integer SAX event and require
   1..UINT64_MAX. This deliberately rejects negative, floating-point, and
   exponent-form numbers.
5. Accept `track` only from the unsigned-integer event, then check
   representability as `std::size_t`.
6. Accept `value` only from signed/unsigned integer events when the value
   fits `int`; reject floating-point events, including exponent notation.
7. Accept `flag` only from the boolean event and `type` only from the
   string event.
8. After parsing, require the exact command-specific field set documented in
   `NATIVE_UI_ADAPTER_CONTRACT.md`, then construct
   `NativeUiCommandMessage`. Do not dispatch from inside the SAX handler.
9. Keep all parsing and allocation on the serialized control thread.

The decoder must report a decode error separately from dispatcher statuses.
The existing typed adapter remains the second validation boundary.

## Required validation before merging the decoder

- Vendor only the pinned v3.12.0 header and its license notice; do not fetch a
  dependency at configure/build time.
- Add a dedicated decoder test target and run the full host CTest suite.
- Add the decoder source and test to the ARMv7 workflow's compile list and
  path filters. Cross-compile with the existing `-std=c++14`,
  `-Wall -Wextra -Werror`, ARMv7 hard-float flags, and runtime symbol checks.
- Test valid messages plus malformed/trailing input, duplicate/unknown keys,
  nested values, wrong JSON types, exponent/fraction syntax, integer
  boundaries/overflow, command-inapplicable fields, missing fields, and
  oversized frames.
- Measure compile time and resulting test-binary size in CI before deciding
  whether further footprint work is needed.

## Scope boundary

This decision does not add the library, parser, IPC, UI toolkit, device
deployment, or hardware tests. It does not change MPC services, audio
settings, ALSA routing, startup files, or the device filesystem.
