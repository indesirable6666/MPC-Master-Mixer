# Native UI adapter contract

## Status and scope

This document specifies the next integration boundary between
`prototype/index.html` and the native mixer controller. It is a design
contract, not an implemented IPC transport. The browser prototype remains
offline and simulated until a separately reviewed native host adapter exists.

The native adapter is the only component allowed to own
`MixerUiCommandDispatcher`, `MixerController`, and the audio command bridge.
The browser must never call the DSP engine or audio callback directly.

## Initial vertical slice

Implement the first live-control slice in this order:

1. Track fader (`SetTrackFader`)
2. Track pan (`SetTrackPan`)
3. Track Mute (`SetTrackMute`)
4. Track Solo (`SetTrackSolo`)
5. Master fader and Master Mute

Do not enable live EQ, Aux, inserts, meters, audio capture, MIDI, or device
routing as part of this first slice. Those features need their own mappings,
validation, and tests.

## Command envelope

A transport message should carry a monotonically increasing unsigned
64-bit `sequence`, a string `type`, a zero-based `track` where relevant,
and the type-specific fields below. The JSON spelling is illustrative; the
native adapter must validate every field and reject unknown keys/types rather
than casting untrusted integers directly to C++ enums.

| UI action | Dispatcher type | Fields | Valid range |
|---|---|---|---|
| Track fader | `SetTrackFader` | `track`, `value` | track 0..15, value 0..127 |
| Track pan | `SetTrackPan` | `track`, `value` | track 0..15, value -100..100 |
| Track Mute | `SetTrackMute` | `track`, `flag` | track 0..15, boolean |
| Track Solo | `SetTrackSolo` | `track`, `flag` | track 0..15, boolean |
| Master fader | `SetMasterFader` | `value` | value 0..127 |
| Master Mute | `SetMasterMute` | `flag` | boolean |

Example request:

```json
{
  "sequence": 41,
  "type": "SetTrackFader",
  "track": 2,
  "value": 96
}
```

Convert the validated envelope into `MixerUiCommand` and call
`MixerUiCommandDispatcher::dispatch()` from one serialized control thread.
Do not call the dispatcher from the audio callback or from multiple
concurrent transport callbacks.

## Response and retry rules

Return the original sequence plus one of these statuses:
`Accepted`, `Invalid`, `OutOfOrder`, `QueueFull`, or `Unsupported`.
Also return `audioTicket` when nonzero.

- `Accepted` means the command entered the native controller/queue; it does
  not mean the audio callback has applied it yet.
- A nonzero `audioTicket` may be polled through the controller's audio-command
  acknowledgement API. Report application only after that acknowledgement.
- `QueueFull` is backpressure. Retain the latest desired value and retry with
  the same sequence after the audio consumer drains the queue.
- `Invalid`, `OutOfOrder`, and `Unsupported` are not retried unchanged.
- Never advance the accepted sequence on a rejected command.
- Coalesce high-rate slider movement to the latest value per control when
  transport is congested; preserve order between different controls and do not
  reuse a sequence after it has been accepted.

## State synchronization

The native view model must be initialized from a
`MixerPresentationPublisher` snapshot, not from hard-coded browser defaults.
After an accepted command, the UI may display a pending state, but must
distinguish it from the last audio-applied state until the returned ticket is
acknowledged. On reconnect, discard stale in-flight requests and request a
fresh snapshot before sending new controls.

Snapshot reads and dispatcher calls must follow the controller's documented
serialized producer/control-thread rule. Do not read mutable controller state
from the audio callback or an unrelated UI thread.

## Safety and real-time boundaries

- The transport thread is not the audio thread.
- No JSON parsing, allocation, logging, socket I/O, UI calls, or waiting is
  permitted in the audio callback.
- Do not expose insert pointers or raw engine setters through the transport.
- Do not claim that demo Web Audio meters are measurements of the native
  signal. Real meter snapshots need a separate bounded, audio-thread-to-control
  publication path.
- Do not open ALSA, change routing, stop MPC services, modify startup files,
  or write to the device filesystem as part of this adapter work.

## Acceptance tests for the first slice

The native adapter should have tests proving:

1. Valid commands map to the exact dispatcher enum and fields.
2. Out-of-range tracks/values, missing fields, wrong JSON types, unknown
   commands, and malformed payloads are rejected before dispatch.
3. Sequence replay and out-of-order delivery do not mutate controller state.
4. Queue-full responses retain the sequence and can be retried after a
   simulated audio block drains the queue.
5. A response is not marked audio-applied until the audio ticket is observed.
6. Reconnect forces a fresh presentation snapshot before controls resume.

The existing `tests/mixer_ui_command_dispatcher_test.cpp` covers the
transport-neutral dispatcher. It does not yet test a JSON parser, IPC channel,
native UI toolkit, or live device.
