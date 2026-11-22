# Native UI adapter contract

## Status and scope

The first transport-independent native command adapter is implemented in
`src/mixer/native_ui_command_adapter.hpp`. It maps a typed, already-decoded
message into the first vertical slice of `MixerUiCommand` and dispatches it
on the serialized control thread.

This is not an IPC transport, native UI toolkit, or device integration. The
browser prototype remains offline and simulated. A strict JSON decoder is now
implemented in `src/mixer/native_ui_json_decoder.hpp`; it validates JSON
syntax, exact JSON value types, duplicate keys, unknown fields, and
command-specific field sets before constructing `NativeUiCommandMessage`.
Presence flags reflect actual field presence; absent values are never replaced
with defaults.

The native adapter is the only component allowed to own
`MixerUiCommandDispatcher`, `MixerController`, and the audio command bridge.
The browser must never call the DSP engine or audio callback directly.

## Implemented first vertical slice

The adapter maps only these commands:

1. Track fader (`SetTrackFader`)
2. Track pan (`SetTrackPan`)
3. Track Mute (`SetTrackMute`)
4. Track Solo (`SetTrackSolo`)
5. Master fader (`SetMasterFader`)
6. Master Mute (`SetMasterMute`)

Track indices are zero-based (0..15), faders are 0..127, and pan is -100..100.
Required-field presence and ranges are checked before dispatch. Unknown command
names are rejected. The adapter does not enable live EQ, Aux, inserts, meters,
audio capture, MIDI, or device routing.

## Message mapping

`NativeUiCommandMessage` is the typed output expected from a future transport
decoder. It carries a monotonically increasing unsigned 64-bit sequence,
command name, and explicit presence flags for wire fields.

| UI action | Dispatcher type | Required fields | Valid range |
|---|---|---|---|
| Track fader | `SetTrackFader` | `track`, `value` | track 0..15, value 0..127 |
| Track pan | `SetTrackPan` | `track`, `value` | track 0..15, value -100..100 |
| Track Mute | `SetTrackMute` | `track`, `flag` | track 0..15, boolean |
| Track Solo | `SetTrackSolo` | `track`, `flag` | track 0..15, boolean |
| Master fader | `SetMasterFader` | `value` | value 0..127 |
| Master Mute | `SetMasterMute` | `flag` | boolean |

Example decoded message:

```json
{
  "sequence": 41,
  "type": "SetTrackFader",
  "track": 2,
  "value": 96
}
```

### Normative decoder rules

A future JSON decoder must enforce these rules before constructing the typed
message. They are part of the wire contract, not optional parser preferences.
The selected parser dependency and SAX implementation constraints are recorded
in [JSON decoder dependency decision](JSON_DECODER_DEPENDENCY_DECISION.md).

- The top-level value must be exactly one JSON object. Reject trailing
  non-whitespace data, malformed JSON, arrays, scalars, and `null`.
- Accept only the fields `sequence`, `type`, `track`, `value`, and
  `flag`. Reject unknown fields and duplicate keys; do not use
  last-key-wins or first-key-wins behavior.
- `sequence` is required and must be a JSON integer in the range
  1..`UINT64_MAX`. Reject negative values, fractions, exponent notation,
  strings, booleans, null, and values that cannot be represented exactly as
  an unsigned 64-bit integer. Do not parse through a floating-point type.
- `type` is required and must be a string matching one of the six command
  names exactly. Reject case variants, whitespace-padded names, non-strings,
  and unknown names.
- `track`, when present, must be a non-negative JSON integer representable
  as `std::size_t`; command-specific range validation then limits it to
  0..15. Reject fractions, exponent notation, negative values, booleans,
  strings, and overflow.
- `value`, when present, must be a signed JSON integer representable as
  `int`; command-specific validation then applies the fader or pan range.
  Reject fractions, exponent notation, booleans, strings, null, and overflow.
- `flag`, when present, must be a JSON boolean. Do not coerce 0/1, strings,
  or null to booleans.
- Required fields are command-specific as shown in the mapping table. Reject
  missing required fields. Also reject fields that are not defined for the
  selected command, rather than silently ignoring contradictory payload data.
- Set each `hasTrack`, `hasValue`, and `hasFlag` flag only after the
  corresponding field has passed exact JSON type and representability checks.
  Never manufacture a default value to make a missing field appear present.
- Bound the transport frame size before parsing. The transport owner must
  choose and document a conservative maximum suitable for these small control
  messages; oversized frames must be rejected before allocating an unbounded
  parse buffer.
- Parsing, allocation, and decoding occur only on the serialized control
  thread, never in the audio callback. The decoder must not call controller
  or engine APIs itself.

`NativeUiJsonDecoder::decode()` returns a distinct decode status to its caller.
Only successfully decoded messages are passed to
`NativeUiCommandAdapter::dispatch()`. The adapter's `Invalid` result remains
the final defense for command-specific UI ranges and dispatch validation.

Call `NativeUiCommandAdapter::dispatch()` from one serialized control
thread. Do not call it from the audio callback or multiple concurrent
transport callbacks.

## Response and retry rules

The adapter returns the original sequence plus one of:
`Accepted`, `Invalid`, `OutOfOrder`, `QueueFull`, or `Unsupported`.
It also returns an `audioTicket` when nonzero.

- `Accepted` means the command entered the native controller/queue; it does
  not mean the audio callback has applied it yet.
- A nonzero `audioTicket` may be polled through the controller's audio-command
  acknowledgement API. Report application only after that acknowledgement.
- `QueueFull` is backpressure. Retain the latest desired value and retry with
  the same sequence after the audio consumer drains the queue.
- `Invalid`, `OutOfOrder`, and `Unsupported` are not retried unchanged.
- Never advance the accepted sequence on a rejected command.

### Pending audio acknowledgement ledger

`NativeUiCommandTracker` is implemented as a fixed-capacity,
allocation-free, control-thread-only ledger. Use its `dispatch(dispatcher,
command, connectionEpoch)` method rather than calling the dispatcher directly:
it checks capacity before dispatch, so an accepted command can never be left
without a retained acknowledgement ticket. A full ledger returns `QueueFull`
before consuming the request sequence. The ledger stores the accepted request
sequence, nonzero audio ticket, and opaque connection epoch.
`popApplied(controller, ack)` releases completed acknowledgements in FIFO
order only after the controller reports the corresponding ticket applied.
The transport must send an acknowledgement only if its stored epoch still
matches the active connection; stale-epoch acknowledgements are discarded,
not forwarded to a reconnected client. Prefer
`popAppliedForEpoch(controller, activeEpoch, ack)` at the transport boundary:
it drains completed stale-epoch entries and returns only an acknowledgement
for the active nonzero epoch. A completed stale entry must still be removed
from the ledger, or repeated reconnects could leave obsolete acknowledgements
occupying its fixed capacity. If the oldest ticket is not yet applied, FIFO
ordering intentionally prevents later acknowledgements from being returned.
Continue polling/draining after disconnect so completed old tickets release
capacity; never erase an unapplied ticket merely because its connection closed.

A command accepted with ticket zero has no asynchronous audio work to track;
the transport may return its accepted response immediately. Rejected results
must not be recorded. If the tracker is full, the caller must apply
backpressure before dispatch rather than accept a command it cannot retain
for acknowledgement. The tracker does not send network responses, create or validate epochs, persist
requests, or own the dispatcher/controller. The transport supplies a nonzero, monotonically increasing epoch on each
connection and uses the epoch returned in each acknowledgement to prevent
stale responses crossing reconnect boundaries. `NativeUiConnectionEpoch` is
implemented as a small control-thread-only epoch source: it issues strictly
increasing nonzero values, permits only one active connection, and invalidates
only the matching active epoch. A delayed disconnect for an older epoch cannot
invalidate a newer connection. It refuses to issue another epoch at
`UINT64_MAX` rather than wrapping to the reserved zero value. The transport
must retain this object across reconnects (or restore its last-issued counter)
for the lifetime of the adapter/sequence space. Epoch zero is rejected before
dispatch and cannot consume a request sequence.

## State synchronization

The native view model must be initialized from a
`MixerPresentationPublisher` snapshot, not hard-coded browser defaults.
The snapshot is a copy of **desired producer state**, not a direct readback of
the audio engine. It now includes:

- `sequence`: monotonically increasing presentation sequence for this
  publisher instance (not a durable session identifier);
- `audioTicketWatermark`: the latest audio command ticket included in the
  snapshot, or zero if no audio command has been queued;
- `audioStateCaughtUp`: true only if the audio callback has acknowledged that
  watermark when the snapshot is published.

Commands are consumed FIFO. Therefore a true `audioStateCaughtUp` means all
audio commands enqueued through that watermark have been applied. If false,
the snapshot still describes the latest desired values, but the consumer must
not claim that all of them are audio-applied. The watermark does not identify
which individual controls have already crossed the block boundary; present
them as pending/unknown until a later snapshot reports caught-up.

After an accepted command, the UI may display a pending state, but must
distinguish it from the last audio-applied state until the returned ticket is
acknowledged. A transport owner should retain each accepted sequence and its
nonzero audio ticket until acknowledgement; commands returning ticket zero
have no asynchronous audio acknowledgement associated with them.

On reconnect, invalidate the old connection epoch and discard stale
client-side in-flight request state. Request a fresh native snapshot before
sending controls. Treat the new snapshot as authoritative for desired state.
If its `audioStateCaughtUp` is false, poll/publish later snapshots on the
serialized control thread until the watermark is acknowledged; do not replay
old commands merely because their acknowledgements were lost during disconnect.
A new transport connection must not reuse old request sequence numbers within
the same adapter lifetime; a restarted adapter establishes a new connection
epoch and starts a fresh sequence space.

Snapshot reads, ticket polling, and dispatcher calls must follow the
controller's serialized producer/control-thread rule. The presentation
sequence is local to one publisher instance and must not be used as a
cross-restart identity or as a substitute for the transport connection epoch.

## Safety and real-time boundaries

- No JSON parsing, allocation, logging, socket I/O, UI calls, or waiting is
  permitted in the audio callback.
- Do not expose insert pointers or raw engine setters through the transport.
- Demo Web Audio meters are not measurements of the native signal.
- Do not open ALSA, change routing, stop MPC services, modify startup files,
  or write to the device filesystem as part of this adapter work.

## Acceptance tests and remaining work

`tests/native_ui_command_adapter_test.cpp` covers command mapping, required
field presence, invalid ranges, unknown command names, sequence ordering,
retry after rejection, and state publication. The existing dispatcher tests
cover sequence ordering, queue backpressure, and audio-ticket application.

`tests/native_ui_command_tracker_test.cpp` covers deferred acknowledgement,
FIFO completion after the audio callback applies queued work, immediate
no-ticket acceptance, rejection handling, and reconnect filtering that drains
old-epoch acknowledgements without exposing them to the active epoch.

`tests/native_ui_connection_epoch_test.cpp` covers monotonic issuance,
single-active-connection enforcement, stale disconnect rejection, invalidation,
and exhaustion without zero wraparound.

`tests/native_ui_json_decoder_test.cpp` covers valid command envelopes,
malformed/trailing input, top-level non-objects, duplicate and unknown keys,
nested values, wrong JSON types, integer boundaries/overflow, missing and
command-inapplicable fields, oversized frames, and exact command-name matching.
The typed adapter remains responsible for fader/pan range validation after
successful decoding.

Still not implemented: IPC, reconnect/snapshot transport, native UI toolkit
integration, and live-device validation. No hardware tests have been performed.


## Reconnect readiness gate

`NativeUiSessionGate` is implemented as a small control-thread-only state
machine layered beside `NativeUiConnectionEpoch`. The epoch source remains
the sole issuer of connection epochs; the gate accepts the already-issued
nonzero epoch and does not generate, persist, or compare epoch ordering.

A new session begins in `Synchronizing`, where audio-affecting commands must
remain disabled. The transport calls `updateSnapshot(epoch, true,
audioStateCaughtUp)` only after it has received and validated a complete
native snapshot for that connection. A full snapshot with
`audioStateCaughtUp == false` updates displayed desired state but leaves the
gate closed. A later complete snapshot whose audio watermark is acknowledged
moves the gate to `Ready`; only then may the transport dispatch commands for
that epoch. Partial snapshots, zero epochs, and snapshots tagged with a stale
epoch are rejected without changing readiness.

On disconnect, invalidate the same epoch in both the epoch source and the
session gate. A delayed disconnect for an old epoch cannot close a newer
session. On reconnect, begin a new epoch, fetch a fresh full native snapshot,
and wait for its audio watermark to catch up. Do not replay client commands
from the previous connection: a lost response does not prove that the native
command was not applied. Continue draining the command tracker's old tickets
after disconnect, but never forward acknowledgements tagged with an obsolete
epoch.

The gate is deliberately transport-neutral. It does not own the dispatcher,
snapshot publisher, epoch source, tracker, socket, or UI; its caller is
responsible for serializing calls on the control thread and checking
`acceptsCommands(activeEpoch)` immediately before dispatch. It does not
establish an IPC implementation or device integration.

`tests/native_ui_session_gate_test.cpp` covers disconnected and synchronizing
states, rejection of partial and stale snapshots, transition to ready only
after audio catch-up, stale disconnect protection, and reconnect isolation.
