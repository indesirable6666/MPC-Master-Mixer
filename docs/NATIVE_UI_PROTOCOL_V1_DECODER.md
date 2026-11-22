# Native UI protocol v1 command decoder

The strict command decoder is implemented in `src/mixer/native_ui_protocol_v1_decoder.hpp`.
It is deliberately separate from the legacy unversioned
`NativeUiJsonDecoder`.

This first parser tranche accepts only the v1 `command` message. It validates
the exact common envelope (`version=1`, `kind=command`, nonzero uint64
`epoch`, nonzero uint64 `sequence`) and the exact field set for the six
supported mixer commands. Duplicate/unknown keys, nested values, wrong JSON
types, floating-point integers, integer overflow, missing/extra fields,
trailing data, and payloads above 1024 bytes are rejected.

The decoder does not decide whether an epoch is current or whether a sequence
is in order. Those checks remain in `NativeUiSessionCoordinator` and the
dispatcher. Values representable as C++ `int` are passed to
`NativeUiCommandAdapter`, which performs the mixer-specific range checks.

Only command decoding is implemented here. Hello, snapshot, result, ACK, and
protocol-error serialization/decoding remain future work. No socket, IPC,
native UI, or target-device integration is provided.
