# Native UI protocol v1 client-message decoder

Implements strict parsing for client-originated `hello` and `snapshot_request`.
Hello requires version 1, epoch exactly zero, and a non-empty UTF-8 client
identifier no longer than 64 bytes. Snapshot requests require a nonzero epoch
and nonzero unsigned 64-bit request identifier.

Duplicate/unknown keys, missing or extra fields, nested values, wrong types,
floating-point values, integer overflow, trailing JSON, and payloads above
1024 bytes are rejected. Output is committed only after full validation.
Per-connection request monotonicity and epoch freshness remain the
transport/session layer's responsibility.

The command decoder is unchanged. Server hello, snapshots, command results,
audio acknowledgements, protocol-error serialization, sockets/IPC, and device
integration are not implemented here.
