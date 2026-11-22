# Native UI adapter contract

## Status and scope

The first transport-independent native command adapter is implemented in
`src/mixer/native_ui_command_adapter.hpp`. It maps a typed, already-decoded
message into the first vertical slice of `MixerUiCommand` and dispatches it
on the serialized control thread.

This is **not** a JSON parser, IPC transport, native UI toolkit, or device
integration. The browser prototype remains offline and simulated. A future
transport decoder must validate JSON syntax, exact JSON value types, duplicate
keys, and unknown fields before constructing `NativeUiCommandMessage`.
Presence flags must reflect actual field presence; absent values must not be
silently replaced with defaults.

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

## State synchronization

The native view model must be initialized from a
`MixerPresentationPublisher` snapshot, not hard-coded browser defaults.
After an accepted command, the UI may display a pending state, but must
distinguish it from the last audio-applied state until the returned ticket is
acknowledged. On reconnect, discard stale in-flight requests and request a
fresh snapshot before sending new controls.

Snapshot reads and dispatcher calls must follow the controller's serialized
producer/control-thread rule.

## Safety and real-time boundaries

- No JSON parsing, allocation, logging, socket I/O, UI calls, or waiting is
  permitted in the audio callback.
- Do not expose insert pointers or raw engine setters through the transport.
- Demo Web Audio meters are not measurements of the native signal.
- Do not open ALSA, change routing, stop MPC services, modify startup files,
  or write to the device filesystem as part of this adapter work.

## Acceptance tests and remaining work

`tests/native_ui_command_adapter_test.cpp` covers command mapping, required
field presence, invalid ranges, unknown command names, and state publication.
The existing dispatcher tests cover sequence ordering, queue backpressure,
and audio-ticket application.

Still not implemented: JSON decoding, IPC, reconnect/snapshot transport,
native UI toolkit integration, and live-device validation. No hardware tests
have been performed.
