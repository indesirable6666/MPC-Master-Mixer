# Native UI protocol v1 client message handler

## Scope

`NativeUiProtocolV1ClientMessageHandler` is the control-thread ingress for the
two client-originated v1 messages: `hello` and `snapshot_request`. It composes
the strict client-message decoder, server-issued session epoch, server hello
serializer, and epoch-scoped snapshot request tracker.

It does not own a socket, read or write a descriptor, publish a mixer
snapshot, dispatch mixer commands, or access the audio callback.

## Behavior

- A valid client `hello` is accepted only when no session epoch is active.
- The handler asks `NativeUiSessionCoordinator` to issue a fresh epoch and
  returns a serialized server `hello` response.
- The client cannot choose the epoch. Client hello must carry epoch zero.
- A second hello while a session is active returns Busy without changing the
  existing response buffer or epoch.
- A snapshot request must name the active epoch and pass the strict increasing
  request-ID tracker. Gaps are allowed; zero, duplicates, decreasing IDs, and
  stale epochs are rejected.
- Only one snapshot request may be in flight. A later request receives Busy
  until the accepted request has been prepared and its complete frame has been
  delivered. Busy does not consume the later request ID.
- `prepareSnapshot(epoch, request, publisher, session, delivery, output)`
  prepares only the exact accepted request. A successful prepare reserves that
  request until `markSnapshotDelivered` confirms the full frame was sent.
  Failed preparation leaves the accepted request retryable.
- `markSnapshotDelivered` validates epoch and request identity and delegates
  delivery confirmation to `NativeUiProtocolV1SnapshotDelivery`; only then
  can another request be accepted. This prevents accepted request IDs from
  silently accumulating while the single-slot delivery object is occupied.
  A mismatched request or frame sequence leaves the prepared request and
  pending delivery intact, so the transport can retry the exact confirmation
  after completing the correct frame.
- `invalidate(epoch, session)` closes the exact active session and resets
  request tracking, but leaves snapshot-delivery cleanup to its owner.
- Prefer `invalidate(epoch, session, delivery)` when the connection owns a
  `NativeUiProtocolV1SnapshotDelivery`: it also cancels an unsent snapshot for
  that epoch. The transport must discard any queued old-epoch frame before
  calling it. Neither overload clears unapplied audio tickets held by the
  session coordinator.

## Threading and validation

All methods are intended for one serialized control/transport thread. The
handler performs no socket I/O and must never be called from the audio callback.
The caller must preserve bounded frame sizes and must not mark a snapshot as
delivered merely because serialization succeeded; delivery is a transport
event.

The host test covers hello issuance, overlapping hello rejection, stale epoch,
snapshot request backpressure, exact prepare/deliver matching, request gaps, disconnect,
and malformed JSON. The same test is compiled by the ARMv7 cross-toolchain
workflow; this is not an on-device execution test.
