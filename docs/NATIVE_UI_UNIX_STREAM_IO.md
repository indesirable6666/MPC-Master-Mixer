# Native UI Unix stream I/O adapter

## Scope

`NativeUiUnixStreamIo` is a small POSIX byte-stream I/O adapter for an
already-open descriptor. It does not create, bind, connect, listen, accept,
close, or own sockets, and it does not choose a socket path or permissions.
Those lifecycle and security decisions remain deliberately unimplemented
until the MPC target environment and process trust model are confirmed.

The adapter is transport/control-thread only. Never call it from an audio
callback. It performs one successful partial read or write per call, retries
`EINTR`, and reports `EAGAIN`/`EWOULDBLOCK` as backpressure. The caller
must retain offsets and retry remaining bytes; a single call is not a complete
frame transfer. A zero-byte receive is peer EOF. Writes use `MSG_NOSIGNAL`
where available so a disconnected peer does not raise SIGPIPE.

## Status values

- `Progress`: `bytes` reports the positive partial transfer, or zero for a
  requested zero-length operation.
- `WouldBlock`: retry when the descriptor becomes writable/readable.
- `PeerClosed`: receive observed orderly EOF, or send returned zero.
- `Error`: inspect `errorNumber`; invalid buffers report `EINVAL`.

The caller combines this adapter with `NativeUiFrameReader`, bounds all
buffers, handles disconnects, and feeds complete payloads to protocol
decoders. No parser, allocator, session coordinator, or audio engine is
invoked here.

## Validation

The host test uses an `AF_UNIX` `socketpair` only inside the test process to
verify transfer, nonblocking backpressure, zero-length calls, invalid input,
peer EOF, bad descriptors, and a write after peer shutdown. On platforms
with `MSG_NOSIGNAL`, the latter verifies that the adapter reports `EPIPE`
without raising `SIGPIPE` in the process. It does not open a filesystem
socket, connect to a device, or interact with MPC services.

Cross-compilation, if added to CI, proves only that the code compiles for the
selected ARM hard-float toolchain. It does not prove that the MPC kernel,
libc, process permissions, or deployment environment support this transport.
