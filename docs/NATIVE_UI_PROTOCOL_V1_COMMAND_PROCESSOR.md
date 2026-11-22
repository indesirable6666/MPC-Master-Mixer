# Native UI protocol v1 command processor

## Scope

`NativeUiProtocolV1CommandProcessor` is the control-thread ingress for v1
mixer command messages. It connects the strict v1 command decoder, typed
command adapter, session readiness/epoch gate, bounded audio-ticket tracker,
and command-result serializer.

## Processing order

1. Strictly decode the JSON command envelope (maximum 1024 bytes).
2. Reject a stale or inactive connection epoch.
3. Reject commands until the current session has received a complete,
   audio-caught-up snapshot.
4. Convert the six supported first-slice commands to typed mixer commands:
   track fader, pan, mute, solo, Master fader, and Master mute.
5. Dispatch through `NativeUiSessionCoordinator`, which reserves bounded
   acknowledgement capacity and records any nonzero audio ticket.
6. Serialize a `command_result` response. An accepted result means the
   control command was queued/accepted; it does not mean the audio callback has
   applied it.

The caller sends the returned response through the transport. Later, after the
audio callback applies a ticket, it drains `session.popApplied()` and sends
the corresponding `audio_ack` message. Queue-full rejection does not consume
the command sequence, allowing the same sequence to be retried.

## Boundaries

This processor does not own or access sockets, descriptors, UI widgets, the
audio callback, or hardware. It must run on the serialized control/transport
thread. EQ, Aux, insert, and other commands remain outside this initial v1
command envelope and are not silently mapped to another operation.

The host test covers not-ready and stale-session rejection, accepted command
dispatch, out-of-order results, malformed track index rejection, and delayed
audio acknowledgement. The ARMv7 workflow compiles the same test; no MPC
hardware execution is implied.
