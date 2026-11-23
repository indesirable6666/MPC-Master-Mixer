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
hello/snapshot/command/audio-ack path over an in-process `AF_UNIX` socketpair:

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
   snapshot frame as sent.
6. A framed command is dispatched and its `command_result` is returned
   independently of audio application. No ACK is available before the callback.
7. After the callback applies the ticket, the control thread stages and sends
   `audio_ack`. The pending acknowledgement is consumed only after the
   writer reports `Sent` and the peer validates the frame.
8. A second prepared but queued snapshot and a staged audio ACK are cancelled
   during disconnect teardown. The control-thread owner cancels the ACK publisher,
   then invalidates the connection; queued bytes and staged response state do not
   cross the epoch boundary.

The socketpair test is host-only and uses no filesystem endpoint or external
service. The ARM workflow validates the relevant portable components and
toolchain probes separately; it does not claim to execute this host socketpair
integration test on ARM hardware.

The ACK publisher uses a peek/serialize/send/consume sequence. The staged ACK
does not consume its ticket; only a complete framed write reported as
`Sent` permits `markDelivered`. Serialization failure, a busy writer,
partial write, or disconnect therefore cannot silently discard an
undelivered acknowledgement.

This remains a host/ARM compile validation of transport-neutral components.
It does not create or bind a Unix socket, launch a service, integrate the
browser UI, touch MPC startup/services/audio configuration, or validate on
physical MPC hardware. No native device operation is implied.


## Protocol-aware disconnect owner

`NativeUiUnixStreamProtocolOwner` is the control-thread teardown boundary for
an already-adopted stream connection. Its `disconnect(epoch)` sequence is
intentionally ordered:

1. Reject a stale epoch or an already-disconnected connection without changing
   the current session.
2. Cancel the stream writer's queued frame bytes.
3. Reset hello/snapshot request tracking and cancel any prepared snapshot.
4. Cancel any staged audio ACK without consuming the session's applied-ticket
   record.
5. Invalidate the connection session, reset the receive pump, and release the
   owned descriptor.

This class does not create, bind, listen, accept, or connect sockets, and it
does not own a polling loop or application event source. The application
transport thread must supply an already-connected descriptor and drive receive,
dispatch, send, and disconnect calls. The browser prototype and real MPC audio
engine remain unconnected.


The disconnect/reconnect regression also leaves an already-applied but
undelivered ACK in the session tracker when the staged publisher is cancelled.
After a fresh epoch is adopted, the ACK publisher must discard that obsolete
epoch entry before it can publish anything for the new connection. The test
asserts that no stale ACK payload is produced and the obsolete ledger entry is
drained. This preserves the distinction between cancelling a transport-stage
copy and consuming an acknowledgement as delivered.


## Explicit-step stream protocol driver

`NativeUiUnixStreamProtocolDriver` composes the existing connection, ingress,
command, snapshot, ACK, and teardown owners behind a caller-driven API. It
provides separate steps rather than starting a thread or polling source:

1. `receiveOne()` incrementally reads until one complete frame is available
   or the nonblocking descriptor would block.
2. `dispatchOne()` handles exactly that frame as a hello, snapshot request,
   or command. It will not dispatch another frame while a response is staged
   or the single-frame writer is occupied.
3. `prepareNextResponse()` stages at most one deferred snapshot or applied
   audio ACK when no response is already staged.
4. `flushOne()` queues and advances the current response. Snapshot readiness
   and ACK consumption are committed only after the writer reports `Sent`.
5. `disconnect(epoch)` delegates ordered teardown to the protocol owner and
   clears the driver's staged metadata only when that epoch is actually closed.

The driver uses the existing caller-provided frame buffers and bounded
single-frame writer. JSON work remains on the control/transport thread; the
audio callback does not call the driver or serialize protocol messages. The
embedding application remains responsible for descriptor creation, readiness
notification, scheduling these steps, and choosing its error/reconnect policy.

A socketpair host test, `native_ui_unix_stream_protocol_driver_test`, walks
through hello, snapshot synchronization, command result, audio callback
application, and delivered ACK using the driver's public steps. This is an
explicit-step integration pilot only: it does not create a production socket,
run an event loop, attach to MPC startup, or connect the prototype UI to the
native engine.


The driver socketpair test also constrains the peer receive window to exercise a
real partial outbound snapshot write. It verifies that `flushOne()` returns `WouldBlock` with a positive byte count,
the writer retains the remaining framed bytes, and disconnect clears both the queued remainder and the driver's staged
response before the connection can be reused.
