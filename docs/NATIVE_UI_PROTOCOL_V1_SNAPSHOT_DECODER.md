# Native UI protocol v1 full snapshot decoder

`NativeUiProtocolV1SnapshotDecoder` decodes one bounded JSON snapshot payload
into a `MixerPresentationSnapshot`.

- Payload limit: 32,768 bytes, excluding the four-byte frame prefix.
- The caller supplies the active connection epoch and outstanding request ID;
  both must be nonzero and must match the message exactly.
- The envelope and every nested object use exact field sets. Unknown, missing,
  and duplicate keys are rejected.
- Track, Aux return, EQ band, insert occupancy, and Audio Input channel arrays
  must have their protocol-defined fixed lengths.
- Integer fields reject floating-point JSON numbers, negatives where unsigned
  is required, and values outside their declared ranges.
- EQ values must be finite and within the engine's frequency, Q, and gain
  limits. Configured track EQ and both Audio Input EQ channels also enforce
  Nyquist against their published sample rate. An unconfigured track must
  publish sample rate zero and is not subjected to a Nyquist check.
- The decoder writes to a temporary candidate and changes the caller's output
  only after the complete message passes validation.
- JSON parsing and allocation are control/transport-thread work only; this
  component performs no I/O and must never run in the audio callback.

The focused host test exercises serializer/decoder round-trip, epoch and
request matching, duplicate and unknown keys, wrong numeric types, array
lengths, range failures, malformed/trailing JSON, payload bounds, and
output preservation on failure. The ARMv7 workflow compiles the same test.

This decoder does not implement framing, sockets, IPC, session synchronization,
native UI integration, or target-device validation.
