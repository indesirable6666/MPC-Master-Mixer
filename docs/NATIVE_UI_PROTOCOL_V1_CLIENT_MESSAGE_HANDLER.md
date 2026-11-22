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
- An accepted snapshot request returns its epoch and request ID to the caller.
  The transport/control loop remains responsible for obtaining a fresh
  `MixerPresentationSnapshot`, serializing it, queueing it, and marking the
  snapshot delivered to the session gate only after the complete frame has
  actually been sent.
- `invalidate(epoch)` closes only the exact active session and resets request
  tracking. It does not clear unapplied audio tickets held by the session
  coordinator.

## Threading and validation

All methods are intended for one serialized control/transport thread. The
handler performs no socket I/O and must never be called from the audio callback.
The caller must preserve bounded frame sizes and must not mark a snapshot as
delivered merely because serialization succeeded; delivery is a transport
event.

The host test covers hello issuance, overlapping hello rejection, stale epoch,
snapshot request acceptance, duplicate rejection, request gaps, disconnect,
and malformed JSON. The same test is compiled by the ARMv7 cross-toolchain
workflow; this is not an on-device execution test.
