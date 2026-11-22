# Native UI protocol v1 — integrated session test

The host integration test now exercises the protocol pieces together rather than
calling the session coordinator directly:

1. A client `hello` is decoded and the server assigns a fresh connection epoch.
2. Two ordered `snapshot_request` messages are accepted and full snapshots are
   serialized and decoded.
3. Snapshot delivery is explicitly confirmed before the session gate can use it.
4. A snapshot whose audio ticket watermark is still pending does not open the
   command gate. After the audio callback applies that ticket, a newer delivered
   snapshot allows commands.
5. A v1 track-fader command is decoded, dispatched, and returned as
   `command_result`.
6. The test confirms no `audio_ack` exists before callback application, then
   serializes the applied acknowledgement after the ticket is observed.
7. Disconnect invalidates the epoch and request sequence.

This remains a host/ARM compile validation of transport-neutral components.
It does not create or bind a Unix socket, launch a service, integrate the
browser UI, touch MPC startup/services/audio configuration, or validate on
physical MPC hardware. No native device operation is implied.
