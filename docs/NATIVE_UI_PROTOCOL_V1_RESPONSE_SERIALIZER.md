# Native UI protocol v1 response serializer

This control-thread utility serializes the four bounded server response
envelopes specified in `NATIVE_UI_PROTOCOL_V1.md`:

- server `hello` with a nonzero epoch and status `ready`;
- `command_result` with nonzero sequence, exact status token, and mandatory
  unsigned 32-bit audio ticket (zero for synchronous/no-ticket results);
- `audio_ack` with nonzero epoch, sequence, and ticket, status `applied`;
- `protocol_error` with a known v1 code and boolean fatal flag.

It uses the vendored JSON library for escaping and integer-safe serialization.
Invalid required identifiers are rejected without changing the caller's output.
The serializer performs no I/O and must only be called on the serialized
control/transport thread, never from the audio callback.

The response-envelope serializer does not implement a socket, IPC endpoint,
session policy, native UI, or target-device integration.
