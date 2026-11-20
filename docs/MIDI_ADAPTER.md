# MIDI adapter — isolated development milestone

## Scope

This milestone defines a small transport boundary between mixer controls and MIDI messages. It is deliberately independent of ALSA and the MPC process.

- `MidiTransport` is an injectable interface.
- `SimulatedMidiTransport` records messages in memory only.
- `VolumeMidiAdapter` converts one normalized UI volume (0.0–1.0) to a 7-bit MIDI CC value.
- `TrackVolumeMidiAdapter` stores 16 independent track mappings (zero-based indices 0–15), each disabled until explicitly verified. It supports clearing one track or all tracks.
- A mapping is disabled by default. It is accepted only when a caller explicitly marks it as verified.
- Invalid values, unverified mappings and missing mappings produce no message.

## Important limitations

This is **not** a live MIDI implementation. It does not create an ALSA sequencer client, open MIDI ports, connect to MPC ports, send messages to hardware, or read feedback from MPC.

The user has confirmed that MPC Volume can be assigned through MIDI Learn and that the assignment persists after saving and reopening a project. The exact MIDI channel, CC number, per-track addressing and feedback behavior have not yet been verified. Therefore this code contains no default CC mapping.

## Automated tests

The host-side test executable checks:

1. No mapping means no outgoing message.
2. Unverified and out-of-range mappings are rejected.
3. A deliberately configured mapping produces the expected CC bytes in the simulated transport.
4. Volume values outside 0.0–1.0 are rejected.
5. Clearing a mapping disables subsequent sends.
6. Each of 16 track slots has an independent mapping; invalid track indices are rejected.
7. Clearing or invalidating one track does not change another track's mapping.
8. Transport failures are returned to the caller.

Build and run locally:

```sh
g++ -std=c++14 -Wall -Wextra -Werror -Isrc/midi tests/midi_adapter_test.cpp -o midi_adapter_test
./midi_adapter_test
```

No MPC hardware is needed. Do not interpret these tests as proof of live MIDI compatibility.
