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
   inspects the applied acknowledgement without consuming it, serializes the
   response, and only then removes the ticket from the pending ledger.
7. Disconnect invalidates the epoch and request sequence, and cancels any
   prepared-but-unsent snapshot for that connection.

A companion transport integration test,
`native_ui_unix_stream_protocol_integration_test`, exercises the same
hello/snapshot path over an in-process `AF_UNIX` socketpair:

1. The connection owner adopts a socketpair endpoint and assigns its epoch.
2. The peer sends a framed hello; the receive pump and protocol handler
   produce a framed server hello.
3. After reconnect, a duplicate hello is rejected, an old-epoch snapshot
   request is rejected, and a current-epoch request is accepted through the
   receive pump.
4. The publisher prepares a snapshot, the bounded writer sends the complete
   frame, and the peer validates the snapshot kind, epoch, request, sequence,
   and track array.
5. The session is marked ready only after the writer reports the complete
   frame as sent. A second prepared but queued snapshot is cancelled when the
   connection is invalidated; queued bytes and pending delivery do not cross
   the epoch boundary.

The socketpair test is host-only and uses no filesystem endpoint or external
service. The ARM workflow validates the relevant portable components and
toolchain probes separately; it does not claim to execute this host socketpair
integration test on ARM hardware.

The ACK publisher uses a peek/serialize/consume sequence. If response
construction fails, the ticket remains pending for a later retry; the output
buffer is changed only after successful serialization and consumption.

This remains a host/ARM compile validation of transport-neutral components.
It does not create or bind a Unix socket, launch a service, integrate the
browser UI, touch MPC startup/services/audio configuration, or validate on
physical MPC hardware. No native device operation is implied.
