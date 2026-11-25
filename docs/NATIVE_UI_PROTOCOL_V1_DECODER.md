# Native UI protocol v1 command decoder

The strict command decoder is implemented in
`src/mixer/native_ui_protocol_v1_decoder.hpp`. It is separate from the
legacy unversioned `NativeUiJsonDecoder` and accepts one bounded v1 JSON
`command` envelope.

## Common envelope

Every command contains exactly:

- `version`: integer 1;
- `kind`: string `"command"`;
- `epoch`: nonzero unsigned 64-bit integer;
- `sequence`: nonzero unsigned 64-bit integer;
- `type`: one of the supported command names below.

The parser rejects duplicate or unknown keys, nested values, wrong JSON
primitive types, floating-point integers, integer overflow, missing/extra
fields, trailing data and payloads larger than 1024 bytes. It parses into a
temporary value and leaves the caller's output unchanged on failure.

## Supported command fields

| Command type | Additional exact fields |
|---|---|
| `SetTrackFader`, `SetTrackPan` | `track`, `value` |
| `SetTrackMute`, `SetTrackSolo` | `track`, `flag` |
| `SetTrackAuxSend` | `track`, `index`, `value` |
| `SetTrackEqBand` | `track`, `index`, `frequencyHz`, `q`, `gainDb`; optional `sampleRate` |
| `SetMasterFader` | `value` |
| `SetMasterMute` | `flag` |
| `SetAudioInputPairLinked`, `SetAudioInputPairStereoMute` | `flag` |
| `SetAudioInputPairStereoGain`, `SetAudioInputPairBalance` | `value` |
| `SetAudioInputChannelGain`, `SetAudioInputChannelPan` | `track` (side 0 or 1), `value` |
| `SetAudioInputChannelMute`, `SetAudioInputChannelSolo` | `track` (side 0 or 1), `flag` |
| `SetAudioInputChannelAuxSend` | `track` (side 0 or 1), `index`, `value` |
| `SetAudioInputChannelEqBand` | `track` (side 0 or 1), `index`, `frequencyHz`, `q`, `gainDb`, required `sampleRate` |

`index` is the zero-based Aux bus or EQ band as appropriate. Track EQ's
omitted sample rate is interpreted as 48 kHz by the typed adapter. Audio In
EQ always carries an explicit sample rate so the target-side Nyquist check
does not depend on an implicit host default.

## Validation boundaries

The decoder validates envelope shape, exact field presence, JSON types and
representability in the typed C++ message. It intentionally does not decide
whether an epoch is current, whether a command sequence is next, whether the
target index is within the model's track/bus/band count, or whether an EQ
frequency is valid for its sample rate. Those checks belong to
`NativeUiSessionCoordinator`, `NativeUiCommandAdapter` and
`MixerUiCommandDispatcher`.

The transport-neutral dispatcher also defines Aux Return control, but the
current v1 wire decoder does not accept an Aux Return command. Insert enable
is deliberately unsupported because a JSON boolean cannot safely construct
or transfer ownership of a native effect object.

## Related v1 messages and integration boundary

Hello and snapshot-request messages use
`NativeUiProtocolV1ClientMessageDecoder`. Snapshots have separate bounded
serializer/decoder classes and carry 16 tracks, four logical Aux Returns,
Master and Audio In pair state. Command results, audio-applied acknowledgements
and protocol errors have a response serializer. A caller-driven Unix-stream
protocol driver composes these control-thread components with the tested
framing/connection abstractions.

These are repository-side software contracts. No socket listener, deployed
native UI process, browser binding, MPC endpoint, physical audio routing,
live MIDI or hardware feedback is claimed. Host tests use synthetic memory;
the ARMv7 workflow cross-compiles selected test programs but does not execute
them on an MPC.
