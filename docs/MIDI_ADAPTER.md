# MIDI adapter — isolated development milestone

## Scope

This milestone defines a small transport boundary between mixer controls and MIDI messages. It is deliberately independent of ALSA and the MPC process.

- `MidiTransport` is an injectable interface.
- `SimulatedMidiTransport` records messages in memory only.
- `VolumeMidiAdapter` converts one normalized UI volume (0.0–1.0) to a 7-bit MIDI CC value.
- `TrackVolumeMidiAdapter` stores 16 independent track-volume mappings (zero-based indices 0–15).
- `TrackPanMidiAdapter` stores 16 independent track-pan mappings. Pan is normalized to [-1,1], with left=-1, center=0 and right=1; values map to MIDI CC values 0, 64 and 127 respectively.
- Every mapping is disabled by default and accepted only when a caller explicitly marks it as verified.
- Invalid values, unverified mappings and missing mappings produce no message. Clearing a track or all tracks disables the corresponding mappings.

## Important limitations

This is **not** a live MIDI implementation. It does not create an ALSA sequencer client, open MIDI ports, connect to MPC ports, send messages to hardware, or read feedback from MPC.

The exact MIDI channel, CC number, per-track addressing and feedback behavior have not been verified. No default CC mapping is provided. The normalized pan range and conversion are adapter conventions, not a claim about MPC's internal pan representation.

## Automated tests

The host-side test executable checks:

1. No mapping means no outgoing message.
2. Unverified and out-of-range mappings are rejected.
3. Deliberately configured mappings produce expected CC bytes in the simulated transport.
4. Volume and pan values outside their supported normalized ranges, including non-finite values, are rejected.
5. Clearing a mapping disables subsequent sends.
6. Each of 16 track slots has an independent mapping; invalid track indices are rejected.
7. Clearing one track does not alter another track's mapping.
8. Transport failures are returned to the caller.

Build and run locally:

```sh
g++ -std=c++14 -Wall -Wextra -Werror -Isrc/midi tests/midi_adapter_test.cpp -o midi_adapter_test
./midi_adapter_test
```

No MPC hardware is needed. Do not interpret these tests as proof of live MIDI compatibility.
