# Native UI adapter contract

## Status and scope

The transport-independent native command adapter is implemented in
`src/mixer/native_ui_command_adapter.hpp`. It maps typed, already-decoded
messages into `MixerUiCommand` and dispatches them on the serialized control
thread. The strict protocol-v1 decoder now exposes track Aux/EQ and Audio In
pair/per-side commands through this adapter. The older unversioned JSON decoder
remains narrower and does not expose Audio In controls.

This is not an IPC transport, native UI toolkit, or device integration. The
browser prototype remains offline and simulated. A strict JSON decoder is now
implemented in `src/mixer/native_ui_json_decoder.hpp`; it validates JSON
syntax, exact JSON value types, duplicate keys, unknown fields, and
command-specific field sets before constructing `NativeUiCommandMessage`.
The transport-neutral orchestration boundary is now composed by
`NativeUiSessionCoordinator`. It owns the epoch source, readiness gate, and
acknowledgement tracker, and ensures that dispatch is rejected until the
current connection has supplied a complete, caught-up snapshot. It still does
not implement IPC, a native UI toolkit, or device integration.

The browser must never call the DSP engine or audio callback directly.

## Implemented typed command surface

The adapter maps track fader/pan/Mute/Solo, track Aux Send and EQ band,
Master fader/Mute, and these Audio In operations:

- Pair LINK/UNLINK, shared stereo gain, balance and Mute.
- Per-side gain, pan, Mute and Solo (pan is rejected while linked).
- Per-side Aux Sends and six-band EQ. In linked mode, channel-side gain,
  Mute, Solo, Aux and EQ edits follow the controller's shared-pair semantics.
- Audio In EQ messages require an explicit sample rate; the dispatcher
  validates the complete six-band configuration before queueing it.

Track indices are zero-based (0..15); Audio In side indices use 0 for left and
1 for right. Aux indices are zero-based (0..3), EQ bands are 0..5, faders are
0..127, pan/balance are -100..100, and Aux send levels are 0..100 percent.
Frequency must be at least 20 Hz and below Nyquist, Q 0.1..10, and gain -12..12
dB. The adapter rejects missing, contradictory, or out-of-range typed fields.
Insert enable remains unsupported because effect construction, ownership and
replacement acknowledgement belong to a future native host. These mappings
are not a browser binding, IPC transport, audio capture path, MIDI adapter, or
device route.

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
| Track Aux Send | `SetTrackAuxSend` | `track`, `index`, `value` | track 0..15, Aux bus 0..3, value 0..100% |
| Track EQ band | `SetTrackEqBand` | `track`, `index`, `frequencyHz`, `q`, `gainDb` | track 0..15, band 0..5, frequency 20 Hz..below Nyquist, Q 0.1..10, gain -12..12 dB |
| Master fader | `SetMasterFader` | `value` | value 0..127 |
| Master Mute | `SetMasterMute` | `flag` | boolean |
| Audio In LINK | `SetAudioInputPairLinked` | `flag` | boolean |
| Audio In stereo gain | `SetAudioInputPairStereoGain` | `value` | linked; 0..127 |
| Audio In balance | `SetAudioInputPairBalance` | `value` | linked; -100..100 |
| Audio In stereo Mute | `SetAudioInputPairStereoMute` | `flag` | linked; boolean |
| Audio In channel gain | `SetAudioInputChannelGain` | `track`, `value` | side 0..1, value 0..127 |
| Audio In channel pan | `SetAudioInputChannelPan` | `track`, `value` | unlinked; side 0..1, -100..100 |
| Audio In channel Mute | `SetAudioInputChannelMute` | `track`, `flag` | side 0..1, boolean |
| Audio In channel Solo | `SetAudioInputChannelSolo` | `track`, `flag` | side 0..1, boolean |
| Audio In Aux Send | `SetAudioInputChannelAuxSend` | `track`, `index`, `value` | side 0..1, bus 0..3, 0..100% |
| Audio In EQ band | `SetAudioInputChannelEqBand` | `track`, `index`, `frequencyHz`, `q`, `gainDb`, `sampleRate` | side 0..1, band 0..5, frequency below Nyquist, Q 0.1..10, gain -12..12 dB, sample rate 8..192 kHz |

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

The legacy JSON decoder enforces the following narrower, unversioned wire
rules before constructing its typed message. It remains limited to its
track/master command subset. Protocol v1 uses a separate strict decoder and
now accepts the track Aux/EQ and Audio In commands in the mapping table above.
The selected parser dependency and SAX implementation constraints are recorded
in [JSON decoder dependency decision](JSON_DECODER_DEPENDENCY_DECISION.md).
The selected parser dependency and SAX implementation constraints are recorded
in [JSON decoder dependency decision](JSON_DECODER_DEPENDENCY_DECISION.md).

- The top-level value must be exactly one JSON object. Reject trailing
  non-whitespace data, malformed JSON, arrays, scalars, and `null`.
- Accept only the fields `sequence`, `type`, `track`, `index`, `value`,
  `flag`, `frequencyHz`, `q`, and `gainDb`. Reject unknown fields and duplicate keys; do not use
  last-key-wins or first-key-wins behavior.
- `sequence` is required and must be a JSON integer in the range
  1..`UINT64_MAX`. Reject negative values, fractions, exponent notation,
  strings, booleans, null, and values that cannot be represented exactly as
  an unsigned 64-bit integer. Do not parse through a floating-point type.
- `type` is required and must be a string matching one of the eight command
  names exactly. Reject case variants, whitespace-padded names, non-strings,
  and unknown names.
- `track`, when present, must be a non-negative JSON integer representable
  as `std::size_t`; command-specific range validation then limits it to
  0..15. Reject fractions, exponent notation, negative values, booleans,
  strings, and overflow.
- `index`, when present, must be a non-negative JSON integer representable as
  `std::size_t`; command-specific range validation limits Aux bus indices to
  0..3 and EQ band indices to 0..5. Reject fractions, exponent notation,
  negative values, booleans, strings, and overflow.
- `value`, when present, must be a signed JSON integer representable as
  `int`; command-specific validation then applies the fader or pan range.
  Reject fractions, exponent notation, booleans, strings, null, and overflow.
- `frequencyHz`, `q`, and `gainDb`, when present, must be finite JSON numbers
  representable as `float`; the legacy decoder accepts them only for
  `SetTrackEqBand`. The dispatcher validates frequency against the 48 kHz
  default sample rate and the complete EQ parameter ranges before queueing.
- The typed Audio In EQ adapter and the protocol-v1 decoder require an
  explicit finite `sampleRate` in the 8000..192000 Hz range. The legacy
  unversioned decoder does not expose Audio In EQ.
- `flag`, when present, must be a JSON boolean. Do not coerce 0/1, strings,
  or null to booleans.
- Required fields are command-specific as shown in the mapping table. Reject
  missing required fields. Also reject fields that are not defined for the
  selected command, rather than silently ignoring contradictory payload data.
- Set each presence flag (`hasTrack`, `hasIndex`, `hasValue`, `hasFlag`,
  `hasFrequencyHz`, `hasQ`, `hasGainDb`, and `hasSampleRate`) only after
  the corresponding field has passed exact JSON type and representability
  checks. Never manufacture a presence flag to make a missing required field
  appear present; the track EQ command's omitted sample rate is the sole
  documented default and remains explicitly absent in the decoded message.
- Bound the transport frame size before parsing. The decoder currently enforces
  a 1024-byte maximum; the future transport must apply the same bound while
  receiving frames, before constructing an unbounded input buffer.
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

`tests/native_ui_command_adapter_test.cpp` covers command mapping including four-bus Aux sends, required
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

`tests/native_ui_json_decoder_test.cpp` covers the legacy unversioned command
envelopes and their strict field/type rules. The v1-specific
`tests/native_ui_protocol_v1_decoder_test.cpp` now covers the expanded track
Aux/EQ and Audio In command set, finite numeric fields, required sample rate,
duplicate/unknown fields, and unchanged output after rejection. The typed
adapter remains responsible for fader/pan/Aux-send range validation after
successful decoding; the dispatcher validates EQ frequency, Q, gain, and the
complete six-band configuration before queueing.

Still not implemented: IPC, reconnect/snapshot transport, native UI toolkit
integration, and live-device validation. No hardware tests have been performed.

`NativeUiSessionCoordinator` composes the connection epoch source, snapshot
readiness gate, and pending acknowledgement tracker. Its control-thread-only
API starts and invalidates sessions, accepts presentation snapshots, gates
command dispatch, and exposes only acknowledgements tagged for the active
epoch. It does not own the controller or dispatcher; those remain owned by the
native host and are supplied to dispatch/poll calls. While disconnected, the
coordinator drains completed obsolete tickets without forwarding them. The
coordinator is a guardrail for the future transport owner, not a transport
implementation.

## Reconnect readiness gate

`NativeUiSessionGate` is implemented as a small control-thread-only state
machine layered beside `NativeUiConnectionEpoch`. The epoch source remains
the sole issuer of connection epochs; the gate accepts the already-issued
nonzero epoch and does not generate, persist, or compare epoch ordering.

A new session begins in `Synchronizing`, where audio-affecting commands must
remain disabled. The transport calls
`updateSnapshot(epoch, snapshotSequence, true, audioStateCaughtUp)` only
after it has received and validated a complete native snapshot for that
connection. The snapshot sequence must be nonzero and strictly greater than
the last accepted sequence for that epoch. Duplicate or delayed snapshots are
rejected without changing state, preventing an older not-caught-up snapshot
from downgrading a session that has already become ready. A newer full
snapshot with `audioStateCaughtUp == false` conservatively returns the gate
to `Synchronizing`; a later newer full snapshot whose audio watermark is
acknowledged moves it to `Ready`. Partial snapshots, zero epochs, and
snapshots tagged with a stale epoch are rejected.

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
states, rejection of partial, stale, duplicate, and out-of-order snapshots,
transition to ready only after audio catch-up, stale disconnect protection,
and reconnect isolation.


`tests/native_ui_session_coordinator_test.cpp` covers dispatch rejection
before synchronization, full-snapshot catch-up, ticket acknowledgement,
disconnect/reconnect sequencing, and stale-epoch isolation.
