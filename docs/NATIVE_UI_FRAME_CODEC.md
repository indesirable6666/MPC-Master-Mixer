# Native UI frame codec

## Status

The bounded incremental framing codec is implemented for host-side tests.
It is not a socket transport and does not open files, create endpoints, wait,
allocate memory, parse JSON, or interact with the audio engine.

## Wire header

Each frame is:

1. Four bytes containing an unsigned payload length in network byte order
   (big-endian).
2. Exactly that many payload bytes.

Zero-length frames are invalid. The payload length is checked against both the
caller's configured maximum and the caller-provided buffer capacity immediately
after the complete header is read, before any payload copy occurs.

## API behavior

`NativeUiFrameCodec::encodeLength()` writes a nonzero 32-bit length header;
`decodeLength()` reads it.

`NativeUiFrameReader` receives:
- a caller-owned payload buffer;
- its capacity;
- a finite maximum frame size, which must be nonzero and no greater than the
  buffer capacity or `UINT32_MAX`.

The reader copies bytes incrementally and retains partial header/payload state.
`feed()` reports the number of input bytes consumed and stops at exactly one
complete frame boundary. When it returns `FrameReady`, the caller consumes
`payload()` / `payloadSize()`, calls `reset()`, then feeds any unconsumed
bytes. This permits multiple frames in one read without losing boundary
information.

Statuses:
- `NeedMore`: current header or payload is incomplete;
- `FrameReady`: one complete payload is available;
- `InvalidLength`: zero or over-limit length; connection owner should close,
  not scan for another header;
- `Truncated`: EOF occurred with a partial header or payload;
- `CleanEof`: EOF occurred between frames or after a complete frame;
- `InvalidConfiguration`: buffer or maximum configuration is invalid.

The reader owns no buffer and performs no dynamic allocation. The caller must
keep the buffer alive and must not overwrite it before consuming the frame.
After `InvalidLength`, the reader is terminal; do not call `reset()` to
attempt resynchronization.

## Limits and integration

For legacy command JSON, configure the reader maximum to
`NativeUiJsonDecoder::kMaximumFrameBytes` (1024). Do not reuse that value for
full snapshots. Snapshot size remains a separately measured and configured
limit; no snapshot maximum is selected by this codec.

The codec does not handle partial writes, EINTR, socket errors, timeouts,
UTF-8 validation, JSON parsing, protocol versions, epochs, or message schemas.
Those belong to a future transport/protocol layer and must remain outside the
audio callback.

## Tests

`tests/native_ui_frame_codec_test.cpp` covers header byte order, zero length,
fragmented header and payload, all split points, back-to-back frames, exact
consumption at frame boundaries, over-limit rejection before payload copy,
truncated EOF, clean EOF, and invalid buffer configuration.
