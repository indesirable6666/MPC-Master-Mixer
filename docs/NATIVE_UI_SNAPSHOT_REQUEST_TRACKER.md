# Native UI protocol v1 snapshot request tracker

## Purpose

`NativeUiProtocolV1SnapshotRequestTracker` validates client snapshot request
identifiers independently from mixer command sequences. It is control-thread
only and owns no transport, socket, session coordinator, or snapshot buffer.

## Rules

- `begin(epoch)` accepts only a nonzero epoch and only when no epoch is active.
- `accept(epoch, request)` accepts only the active nonzero epoch and a nonzero
  request identifier strictly greater than the last accepted request.
- A rejected request never advances the watermark.
- Gaps are permitted: request identifiers are correlation tokens, not counts.
- `invalidate(epoch)` clears the tracker only for the currently active epoch.
- A new epoch starts with request watermark zero; identifiers may restart for
  the new connection.
- The tracker does not itself serialize or send snapshots. The transport layer
  must associate an accepted request with the exact response and epoch.

The class uses fixed-width integer state only, performs no allocation, and
returns boolean acceptance results. All methods are intended to be called from
the serialized control/transport thread.

## Validation

`tests/native_ui_protocol_v1_snapshot_request_tracker_test.cpp` covers zero
and mismatched epochs, overlapping begin, zero request, duplicates, decreasing
requests, permitted gaps, invalidation, reconnect reset, and the maximum
unsigned 64-bit request identifier.

Host CMake target:
`native_ui_protocol_v1_snapshot_request_tracker_test`.

The ARMv7 workflow compiles the same test with the configured hard-float
toolchain. This is a cross-compilation check only; the test binary is not run
on an MPC device.
