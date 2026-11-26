# Native UI browser protocol client

## Scope

`prototype/native-ui-protocol-client.js` is a transport-neutral JavaScript
client for the existing Native UI protocol v1. It implements the client-side
session state machine, strict response-envelope checks, full-snapshot
validation, command sequencing, queue-full retries, and applied-audio ACK
tracking.

It is deliberately not a transport implementation. A browser page cannot
open an AF_UNIX stream socket directly. A future native embedding layer must
supply the `send(payload)` callback and feed complete UTF-8 JSON payloads to
`client.receive(payload)`. The four-byte big-endian stream frame prefix is
owned by that embedding transport and must not be passed to the client.

The current `prototype/index.html` remains a local interaction/signal demo.
It does not load this helper, create a native connection, send mixer commands,
or receive device meters. Adding the helper does not make the prototype or
the MPC hardware connected.

## Lifecycle

Create one client per UI process and keep it alive across reconnects when
possible:

```js
const client = new NativeUiProtocolClient({
  send: payload => nativeTransport.sendJsonPayload(payload),
  lastSequence: persistedLastAcceptedSequence
});

client.connect();
// The embedding layer forwards each complete JSON payload:
client.receive(payload);
```

The transport callback receives the JSON body only, without the stream length
prefix. It must return `false` when the payload was not accepted into its
bounded transmit queue. It must not call this API from the audio callback.

The client sends `hello` with epoch zero. On a valid server `hello`, it
automatically sends the first `snapshot_request`. It becomes command-ready
only after a matching full snapshot has passed validation and reports
`audio_state_caught_up: true`. A snapshot with a pending audio watermark is
delivered to `onSnapshot`, but leaves commands gated; the embedding layer may
request a newer snapshot when the server reports that audio state has caught
up.

Callbacks are optional:

- `onState(state)`: connection, readiness, epoch and pending-command counts;
- `onSnapshot(message)`: a complete validated desired-state snapshot;
- `onCommandResult(message)`: accepted or rejected dispatch result;
- `onAudioAck(message)`: matching applied-audio acknowledgement;
- `onProtocolError(message)`: validated server protocol error;
- `onDisconnect()`: logical session closed.

## Sequence and acknowledgement rules

- Epoch, snapshot request and command sequence identifiers are represented
  internally as canonical decimal strings. This avoids JavaScript
  `Number` rounding for protocol uint64 values; outgoing identifiers are
  serialized as JSON integer tokens, not quoted strings.
- Snapshot request identifiers restart at one for each connection epoch.
- Command sequence numbers do not restart on reconnect. Keep the client
  instance alive, or pass its `lastSequence` value when constructing a new
  instance. The embedding layer must persist this value for the lifetime of
  the native dispatcher.
- A `queue_full` result leaves the exact command payload pending.
  `retryCommand(sequence)` resends that same sequence and payload.
- An `accepted` result with a nonzero audio ticket remains pending until a
  matching `audio_ack` with status `applied` arrives. An accepted result
  with ticket zero is complete without an audio ACK.
- Stale epochs, unknown/duplicate fields, malformed snapshots, invalid
  identifiers and mismatched ACK tickets are rejected.

The client validates the v1 command shapes implemented by
`NativeUiProtocolV1Decoder`. It does not invent wire commands for controls
that v1 does not expose, including Aux Return editing and insert replacement.

## Validation

`tests/prototype-native-ui-client.test.cjs` covers handshake and snapshot
gating, 64-bit identifier preservation, command dispatch, queue-full retry,
ACK matching, reconnect sequencing, stale-epoch rejection, malformed input
and fatal protocol errors. The Prototype validation workflow runs these tests
alongside the existing DOM interaction suite.

## Remaining integration work

A native host must still provide an approved transport and lifecycle owner,
marshal incoming complete payloads onto the UI thread, and define how snapshots
are applied to UI controls without echoing them back as new commands. The
current C++ Unix-stream components intentionally accept an already-created
descriptor; they do not create, bind, listen, accept or connect a production
endpoint. No socket path, process identity, permission policy or deployment
model is selected here.

Do not infer audio-device, MIDI, meter-return, MPC process or hardware
integration from the protocol client tests. Those tests use synthetic JSON
and in-memory callbacks only.
