# Native UI protocol v1 command processor

## Scope

`NativeUiProtocolV1CommandProcessor` is the serialized control-thread ingress
for v1 mixer command messages. It connects the strict JSON decoder, typed
command adapter, session epoch/readiness gate, bounded audio-ticket tracker,
and command-result serializer. It owns no socket and performs no work on the
audio callback.

## Supported command families

After the active session has received a complete, audio-caught-up snapshot,
the v1 command processor accepts:

- Track fader, pan, Mute and Solo.
- Track Aux Send and one six-band EQ parameter update (frequency, Q, gain;
  track EQ sample rate defaults to 48 kHz when omitted).
- Master fader and Mute.
- Audio In pair LINK/UNLINK, shared stereo gain, balance and Mute.
- Audio In channel gain, pan, Mute, Solo, Aux Send and six-band EQ. The
  channel `track` field is side 0 (left) or 1 (right); Audio In EQ requires
  an explicit sample rate.
- Linked-mode restrictions are enforced by the dispatcher/controller. For
  example, mono Pan is rejected while the pair is linked; shared gain, Mute,
  Solo, EQ and Aux state remain coordinated.

The strict decoder checks the exact JSON field set and primitive types.
The adapter and dispatcher then validate target indices, parameter ranges,
sample-rate/Nyquist constraints, and current LINK mode. The transport-neutral
dispatcher also has an Aux Return command, but that command is not part of the
current v1 wire decoder. Insert enable remains unsupported on the wire: a
boolean cannot safely construct or transfer ownership of an `AudioInsert*`.

## Processing and acknowledgement

1. Decode one bounded JSON command (maximum 1024 bytes).
2. Reject an inactive or stale connection epoch.
3. Reject commands until the current session has received a complete,
   audio-caught-up snapshot.
4. Convert the decoded message to a typed mixer command.
5. Dispatch on the serialized producer thread. The session reserves bounded
   acknowledgement capacity and records the returned nonzero audio ticket.
6. Serialize a `command_result`. `accepted` means the command was queued;
   it does not mean the audio callback has applied it.
7. After the callback applies the ticket, the control thread publishes the
   corresponding `audio_ack`. The transport consumes that ACK only after the
   complete framed response has been sent.

Queue-full or invalid commands do not consume the next accepted sequence.
The caller must preserve one serialized producer and retry a queue-full
command with the same sequence only after backpressure clears.

## Snapshot synchronization

The v1 snapshot serializer/decoder carry all 16 tracks, four logical Aux
Returns, Master state, and the value-only Audio In pair state (LINK, shared
controls, both channel states, six-band EQ, four Aux sends and insert
occupancy flags). A session becomes ready only after the complete snapshot
frame is reported delivered and its audio ticket watermark is caught up.
Snapshots represent controller desired state plus an explicit applied
watermark; they are not hardware feedback.

## Boundaries and tests

The protocol processor and caller-driven Unix-stream protocol driver are
control-thread components. Framing, partial writes, backpressure, connection
epochs, snapshot delivery and delayed audio ACKs have offline host tests.
The command-processor test also drives Audio In LINK/UNLINK, shared and
per-side controls, EQ/Aux commands and ordinary track EQ/Aux through the
strict decoder and verifies FIFO audio acknowledgements.

These tests establish software contracts with synthetic buffers and host
memory only. They do not establish a deployed application, a browser-to-native
binding, an MPC socket endpoint, physical audio routing, live MIDI, or hardware
feedback. The ARMv7 workflow compiles selected test binaries but does not run
them on the MPC.
