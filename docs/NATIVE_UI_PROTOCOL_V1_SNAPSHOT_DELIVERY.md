# Native UI protocol v1 snapshot delivery

## Purpose

`NativeUiProtocolV1SnapshotDelivery` connects a validated snapshot request to
the presentation publisher, v1 snapshot serializer, and session readiness
gate. It deliberately separates *serialization* from *delivery*.

## Lifecycle

1. The caller accepts a `snapshot_request` through
   `NativeUiProtocolV1ClientMessageHandler`.
2. On the serialized control thread, call `prepare(epoch, request, publisher,
   session, output)`. It publishes a complete desired-state snapshot and
   serializes the v1 response. Only one prepared delivery may be outstanding.
3. Queue/write the framed payload using the transport. Partial writes do not
   count as delivery.
4. Only after the transport confirms that the complete frame has been sent,
   call `markDelivered(epoch, sequence, session)` with the exact epoch and
   snapshot sequence returned by the prepared delivery.
5. The session gate records that complete snapshot. If its audio watermark is
   not yet applied, commands remain disabled. Publish and deliver a newer
   snapshot after the callback catches up; only then can the gate become Ready.

A wrong epoch, wrong sequence, missing pending delivery, or stale connection
cannot open the gate. Failed confirmation leaves the pending delivery intact
so the caller can handle the transport failure. On disconnect, the owner must
invalidate the session, discard any queued frame for that epoch, and call
`cancelPending(epoch)` to release the abandoned snapshot. A new connection can
then prepare its own snapshot without inheriting stale delivery state.

## Boundaries

- Control/transport thread only; never call from the audio callback.
- No socket, descriptor, framing, retry timer, or transport ownership.
- The caller remains responsible for matching the request ID to a request
  accepted by the client-message handler.
- The snapshot is desired producer state, not hardware readback.
- A snapshot that serialized successfully but was not completely transmitted
  must never be marked delivered.

The host test verifies the two-stage delivery contract, stale confirmation
rejection, readiness remaining closed while audio work is pending, and
readiness opening only after a newer caught-up snapshot is fully confirmed, and
an abandoned delivery being cancelled before the next epoch can prepare one.
The ARMv7 workflow compiles the same test; it is not run on the MPC device.
