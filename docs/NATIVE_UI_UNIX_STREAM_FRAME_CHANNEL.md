# Native UI Unix stream frame channel

## Scope and threading

This layer composes the frame codec with the nonblocking POSIX byte I/O
adapter. It does not create/connect/listen/accept sockets, own descriptors,
choose a filesystem endpoint, or change process configuration. The caller
owns the descriptor and all storage. All methods are control/transport-thread
only; never call them from an audio callback.

## Bounded transmit queue

`NativeUiUnixStreamFrameWriter` stores one pending frame in caller-provided
memory. It adds the four-byte big-endian header, rejects zero/oversized frames
and insufficient storage, and reports `Busy` rather than overwriting a
pending frame. `flush()` handles partial writes until the frame is complete
or the nonblocking descriptor would block. The caller retries on writable
readiness. On peer closure or hard error, the connection owner should close
the descriptor and discard this writer state with the connection object.

## Incremental receive pump

`NativeUiUnixStreamFrameReaderPump` reads into a caller-provided bounded
scratch buffer and feeds `NativeUiFrameReader`. If a read contains more than
one frame, bytes after the first complete frame remain buffered until the
caller consumes the payload and calls `acknowledgeFrame()`. Partial headers
and payloads persist across calls. A nonblocking empty read returns
`NeedMore`; EOF is classified by the frame reader as clean or truncated.

The pump uses `InvalidConfiguration` for invalid storage and hard I/O errors;
the owner should treat that as terminal for the connection. Protocol decoding,
epochs, snapshots, acknowledgements, reconnection policy, and endpoint
permissions remain above this layer.

## Validation

The host test uses only an in-process `AF_UNIX` socketpair. It covers queued
writes, busy/size rejection, frame round-trip, short scratch reads, multiple
concatenated frames, nonblocking backpressure, EOF, and invalid descriptors.
No filesystem socket, MPC process, audio device, MIDI port, or hardware is
accessed.
