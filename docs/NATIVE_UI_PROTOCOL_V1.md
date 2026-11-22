# Native UI protocol v1

## Status and scope

**Protocol codecs and transport-neutral session logic implemented; transport and device integration remain unimplemented.** The repository has strict decoders for v1 commands, client `hello` / `snapshot_request`, and full snapshots; serializers cover full snapshots and the server responses `hello`, `command_result`, `audio_ack`, and `protocol_error`. A host integration test exercises snapshot decode, readiness gating, command dispatch, and audio acknowledgement. The socket/IPC endpoint, native UI integration, and device integration are not implemented. The target OS and process permissions remain unverified.

Protocol v1 deliberately exposes only the six commands already supported by
`NativeUiCommandAdapter`: track fader, pan, mute, solo, master fader, and master
mute. Other controls visible in `MixerUiCommandDispatcher` are not implicitly
available over this protocol. In particular, inserts remain unsupported.

The protocol is transport-independent. The proposed four-byte big-endian length
prefix from `NATIVE_UI_IPC_TRANSPORT_DECISION.md` frames UTF-8 JSON payloads; the
prefix is not part of the JSON document.

## Common envelope

Every v1 message is exactly one JSON object with these required top-level fields:

| Field | JSON type | Rule |
|---|---|---|
| `version` | integer | Exactly `1`; no float, string, boolean, or null |
| `kind` | string | Exact message-kind name from this specification |
| `epoch` | integer | Nonzero unsigned 64-bit server-issued connection epoch |

The client sends `epoch: 0` only in `hello`, where it means “not assigned yet”.
The server's `hello` response supplies the nonzero epoch. Every subsequent
message in either direction carries that exact epoch. A client cannot choose,
increment, or replace it. A stale or mismatched epoch invalidates the message;
it must never be reinterpreted as belonging to the current connection.

Each message kind has an exact field set. Reject unknown and duplicate keys,
missing fields, wrong JSON types, nested values where scalars are required,
trailing non-whitespace data, and out-of-range integers. Do not coerce values.
JSON integers must be parsed exactly, not through floating point. Reject NaN
and infinities (which are not valid JSON numbers in any case).

## Message kinds

### 1. Client hello

```json
{"version":1,"kind":"hello","epoch":0,"client":"touch-mixer"}
```

Client fields: `client` is a non-empty UTF-8 string of at most 64 bytes.
No client-provided feature list or epoch is accepted in v1.

### 2. Server hello

```json
{"version":1,"kind":"hello","epoch":7,"status":"ready"}
```

The server issues the epoch through `NativeUiSessionCoordinator::beginConnection`.
`status` is exactly `ready`. If the server cannot accept the connection, it
sends a `protocol_error` where possible and closes it. The server must not
reuse an epoch during the lifetime of the epoch source.

After server hello, the client requests a full snapshot. Commands are not
permitted yet.

### 3. Snapshot request

```json
{"version":1,"kind":"snapshot_request","epoch":7,"request":1}
```

`request` is a nonzero unsigned 64-bit client request identifier, strictly
increasing within the connection. It is for matching responses only; it is not
a mixer command sequence.

### 4. Full snapshot response

```json
{"version":1,"kind":"snapshot","epoch":7,"request":1,"sequence":12,
 "audio_ticket_watermark":0,"audio_state_caught_up":true,"state":{}}
```

The example abbreviates `state`; the actual object is required and contains all
fields below. No partial snapshots exist in v1.

- `sequence`: nonzero unsigned 64-bit presentation sequence from the current
  `MixerPresentationPublisher` instance. It is monotonic within that publisher,
  not a durable identifier and not a replacement for `epoch`.
- `audio_ticket_watermark`: unsigned 32-bit audio command ticket included in
  this desired-state snapshot.
- `audio_state_caught_up`: boolean copied from the publisher.
- `state`: exact mixer state object described below.

The state object has exactly these fields:

| Field | Shape and meaning |
|---|---|
| `tracks` | Array of exactly 16 track objects, index 0..15 |
| `aux_returns` | Array of exactly 4 Aux return objects, index 0..3 |
| `master_fader` | Integer 0..127 |
| `master_mute` | Boolean |
| `audio_input_pair` | Audio Input pair object |

Each track object has exactly:
- `fader`: integer 0..127;
- `pan`: integer -100..100;
- `mute`, `solo`: booleans;
- `aux_sends`: array of exactly 4 integers 0..100;
- `eq`: array of exactly 6 objects, each with `frequency_hz`, `q`,
  and `gain_db` finite JSON numbers;
- `eq_configured`: boolean indicating whether a track EQ configuration has
  successfully been queued;
- `eq_sample_rate`: finite JSON number; exactly `0` while
  `eq_configured` is false, otherwise the actual sample rate supplied with the
  last successfully queued track EQ configuration (8000..192000 Hz);
- `has_insert`: array of exactly 2 booleans (the engine's two insert slots).

Each Aux return object has exactly `level` (integer 0..127),
`balance` (integer -100..100), and `mute` (boolean).

The `audio_input_pair` object has exactly:
- `linked`: boolean;
- `stereo_gain`: integer 0..127;
- `balance`: integer -100..100;
- `stereo_mute`, `stereo_solo`: booleans;
- `stereo_aux_sends`: array of exactly 4 integers 0..100;
- `channels`: array of exactly 2 channel objects, left then right.

Each Audio Input channel object has exactly:
- `gain`: integer 0..127;
- `pan`: integer -100..100;
- `mute`, `solo`: booleans;
- `eq_sample_rate`: finite JSON number in the engine-supported range 8000..192000;
- `aux_sends`: array of exactly 4 integers 0..100;
- `eq`: array of exactly 6 EQ band objects with the same fields and validation;
- `has_insert`: array of exactly 2 booleans (the engine's two insert slots).

For every EQ band, `frequency_hz` must be at least 20; `q` must be 0.1..10;
`gain_db` must be -12..12. When `eq_configured` is true, every track EQ band's
frequency must also be strictly below half its published `eq_sample_rate`.
When false, the EQ bands are the controller's desired default parameters and
are not evidence that an EQ configuration has been applied or queued; do not
apply a Nyquist check against the zero sentinel. Audio Input channel EQ always
has an associated configured sample rate and follows the same Nyquist rule.

All arrays are fixed-length and ordered as specified. Do not serialize C++
object memory, padding, pointers, coefficient state, or insert instances.
`has_insert` is presence metadata only; it does not authorize remote insert
creation, deletion, or pointer access.

A snapshot is a copy of **desired producer state**, not guaranteed hardware
readback. If `audio_state_caught_up` is false, the UI must show the snapshot as
desired/pending and must not claim it is fully applied. The watermark does not
identify individual controls that remain pending.

### 5. Command

```json
{"version":1,"kind":"command","epoch":7,"sequence":41,
 "type":"SetTrackFader","track":2,"value":96}
```

`sequence` is a nonzero unsigned 64-bit mixer command sequence. It must
increase strictly for newly submitted commands during the lifetime of the
native dispatcher. Reconnect does not reset the server's dispatcher sequence;
a client reconnecting to the same adapter must continue with a sequence greater
than its last accepted sequence. A rejected `QueueFull` command may be retried
with the same sequence. Do not resend an accepted command as a new command.

The six supported command forms are exact:

| `type` | Required fields besides envelope | Constraints |
|---|---|---|
| `SetTrackFader` | `track`, `value` | track 0..15; value 0..127 |
| `SetTrackPan` | `track`, `value` | track 0..15; value -100..100 |
| `SetTrackMute` | `track`, `flag` | track 0..15; boolean flag |
| `SetTrackSolo` | `track`, `flag` | track 0..15; boolean flag |
| `SetMasterFader` | `value` | value 0..127 |
| `SetMasterMute` | `flag` | boolean flag |

Reject any extra command field, including `track` on master commands, and any
command name not listed. `track` is a nonnegative integer representable as
`size_t`; `value` is an integer representable as `int`; `sequence` is
exact unsigned 64-bit. The legacy `NativeUiJsonDecoder` accepts an unversioned command object and
is intentionally kept separate. The dedicated `NativeUiProtocolV1CommandDecoder`
validates this exact v1 envelope; do not weaken or silently reinterpret either
decoder.

### 6. Command result

```json
{"version":1,"kind":"command_result","epoch":7,"sequence":41,
 "status":"accepted","audio_ticket":19}
```

`status` is one of `accepted`, `invalid`, `out_of_order`, `queue_full`,
or `unsupported`. `audio_ticket` is an unsigned 32-bit integer and is always
present; zero means no asynchronous audio work was queued. Nonzero tickets
mean the command was accepted into the controller/audio-command path, not
that the audio callback has applied it.

A result is sent for each syntactically valid command that reaches dispatch.
A malformed or unsupported-version message instead receives
`protocol_error` if a safe response can be formed. `QueueFull` is retryable
with the same sequence; other rejection statuses are not retried unchanged.
The tracker must reserve capacity before dispatch so an accepted nonzero ticket
cannot be lost from the acknowledgement ledger.

### 7. Applied-audio acknowledgement

```json
{"version":1,"kind":"audio_ack","epoch":7,"sequence":41,
 "audio_ticket":19,"status":"applied"}
```

This message is sent only after the controller reports that the ticket has been
applied and `NativeUiCommandTracker` returns it for the active epoch.
`status` is exactly `applied`. Acknowledgements preserve tracker FIFO order.
Never send an old-epoch acknowledgement to a new connection. Ticket zero
commands do not produce an `audio_ack`; their accepted `command_result` is
sufficient.

### 8. Protocol error

```json
{"version":1,"kind":"protocol_error","epoch":7,
 "code":"invalid_message","fatal":true}
```

`code` is a short exact machine-readable token. V1 codes are
`unsupported_version`, `invalid_message`, `stale_epoch`,
`not_synchronized`, `message_too_large`, and `server_busy`.
`fatal` is boolean. Fatal errors close the connection after the response;
nonfatal errors leave it open only when framing and message boundaries remain
unambiguous. Do not echo untrusted payload text.

## Synchronization and reconnect state machine

1. TCP/IP is not implied. Establish the approved local transport and receive a
   complete client `hello`.
2. Server issues a fresh epoch and replies with server `hello`.
3. Client sends `snapshot_request`; server publishes a complete snapshot and
   associates it with the request.
4. Client validates every field and array length, then the server-side session
   gate records that the full snapshot was delivered for the active epoch.
5. If the snapshot says audio is not caught up, the server publishes a newer
   full snapshot after polling the watermark. The gate remains synchronizing.
   Only a newer full snapshot with `audio_state_caught_up: true` makes
   `NativeUiSessionCoordinator::isReady()` true.
6. Commands are accepted only while the coordinator is ready for the active
   epoch. Check readiness immediately before dispatch.
7. On disconnect, invalidate that exact epoch. Drain completed stale tracker
   entries, but retain unapplied tickets until they complete. Do not replay
   commands from the old connection after reconnect.
8. Reconnect repeats hello and full snapshot synchronization. A lost response
   does not prove the command was not applied; the client must reconcile from
   the new authoritative desired-state snapshot rather than replay uncertain
   commands.

The snapshot publisher sequence is strictly increasing within one publisher.
The session gate rejects zero, duplicate, and older snapshot sequences; a
newer not-caught-up snapshot returns the gate to synchronizing. Do not compare
presentation sequence values across publisher restarts; the connection epoch
is the cross-connection boundary.

## Limits, parsing, and operational behavior

- V1 command JSON payload maximum: 1024 bytes, matching the existing command
  decoder's current limit. This does **not** define the snapshot maximum.
- Snapshot payload maximum must be a separately configured, finite limit
  selected after measuring the worst-case serialized full snapshot. Reject an
  oversized frame before allocating its body. Record the selected limit and
  test the maximum-size valid snapshot before implementation is considered
  complete.
- All messages use the framing limit configured for their message class.
  Reject zero-length frames and truncated headers/payloads; do not scan for a
  new frame boundary after corruption.
- Bound input and output queues, number of clients (one active client in v1),
  pending requests, and write time. Slow clients must not cause unbounded
  memory growth or block control/audio work.
- JSON parsing, serialization, allocation, socket I/O, logging, and waiting
  occur only on the serialized control/transport thread, never in the audio
  callback.
- A malformed frame, duplicate key, unknown key, wrong type, invalid range,
  unsupported message kind, or stale epoch is rejected deterministically.
  Repeated malformed/fatal protocol violations close the connection.
- The transport must not expose arbitrary controller methods, engine pointers,
  insert pointers, or raw setters.

## Implementation and acceptance checklist

Before implementing v1:
- [x] Resolve track EQ sample-rate provenance in the presentation snapshot;
      unconfigured tracks expose `eq_configured:false` and rate `0`, while
      successfully queued configurations publish their actual requested rate.
- [x] Set a separate 32 KiB full-snapshot payload ceiling; the worst-case valid fixture measured 8,653 bytes on the host CI runner and asserts the serialized payload remains within the limit.
- [x] Implement strict v1 command/client-message decoders and server serializers for the four non-snapshot response kinds.
- [x] Implement bounded full snapshot serialization after sample-rate provenance and payload ceiling are resolved.\n- [x] Implement strict full snapshot decoding with exact field sets, duplicate-key rejection, fixed array lengths, integer/float validation, EQ ranges, epoch/request matching, and unchanged output on failure.
- [x] Add focused golden JSON and malformed-input tests for command, client hello/snapshot request, and the four non-snapshot server response kinds.
- [x] Add serializer fixtures for fixed array lengths, finite EQ values, range rejection, and worst-case maximum-size payload.
- [x] Test duplicate/unknown keys, exact field sets, fixed array lengths,
      integer-vs-float typing, EQ ranges, epoch/request mismatch, malformed JSON,
      and unchanged output on failure. Full boundary/fuzz coverage remains future work.
- [x] Add a reconnect regression test: an unapplied old-epoch ticket remains
      tracked across disconnect, blocks readiness until applied, and its ACK is
      discarded rather than exposed to the next epoch.
- [ ] Test epoch mismatch, snapshot sequencing, readiness gating, command
      sequence retry, queue-full behavior, and delayed audio acknowledgements.
- [ ] Test worst-case frame sizes and partial framing independently of sockets.
- [ ] Only then implement a bounded host-side transport under the separately
  approved IPC deployment model.

No IPC, native UI, target-device connection, or hardware validation is
implemented by this specification. No MPC service, startup file, ALSA route,
activation state, or protected filesystem has been changed.
