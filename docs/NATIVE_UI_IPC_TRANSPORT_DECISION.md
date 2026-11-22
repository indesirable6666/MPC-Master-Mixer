# Native UI IPC transport decision

## Status

**Provisional design decision; no transport implementation is authorized by this
document.** The target device, its running kernel, libc, process model, and
permissions have not been inspected. The assumed MPC Live II / MPC OS 2.15 /
Buildroot environment remains unverified.

The existing JSON decoder, command adapter, session coordinator, and
acknowledgement tracker are transport-neutral. Keep them that way.

## Candidate: local Unix-domain stream socket

For two cooperating processes on the same Linux device, an
`AF_UNIX` stream socket is the preferred candidate because it provides
local bidirectional byte-stream communication without opening a network
listener. It is a candidate, not a confirmed target capability.

Before implementation on the device, verify all of the following from an
authorized development environment:

- The target kernel enables Unix-domain sockets (`CONFIG_UNIX`).
- The target C library exposes the required socket APIs.
- The intended UI and host processes can both access a dedicated socket path.
- The host process can create and safely own that path without changing MPC
  startup files, system services, or protected filesystem locations.
- The socket directory permissions and process identities can enforce the
  intended trust boundary.
- The host application's lifecycle can create, accept, close, and clean up
  the endpoint without interfering with MPC services.

If any prerequisite is not met, stop and revise this decision before coding.
Do not silently fall back to TCP, a world-writable path, or a privileged
system location.

## Framing

A stream socket does not preserve message boundaries. The proposed framing
for a future transport is:

1. Four-byte unsigned payload length in network byte order (big-endian).
2. Exactly that many UTF-8 JSON payload bytes.
3. Reject a zero length and any length greater than the configured maximum
   before allocating or parsing the payload.
4. For the currently implemented command decoder, the maximum payload is
   `NativeUiJsonDecoder::kMaximumFrameBytes` (1024 bytes).
5. Treat EOF in the middle of a header or payload as a truncated frame and
   invalidate the connection. Do not attempt to resynchronize by scanning
   arbitrary bytes.

Reads and writes must handle partial progress, interruption, and orderly
disconnect. A complete frame is passed to the existing JSON decoder on the
serialized control thread. No socket operation, parsing, allocation, or
blocking wait may occur in the audio callback.

The four-byte transport header is not part of the JSON input passed to
`NativeUiJsonDecoder::decode()`.

## Protocol responsibilities still to define

The current JSON decoder accepts only the six command messages. It is not a
general protocol parser. Before implementing a transport, define and test:

- Protocol version negotiation and an explicit client/server hello.
- A full presentation-snapshot wire schema, including all fields needed by
  the native UI; do not serialize C++ structs by copying their memory.
- Distinct message types for command, command result, applied-audio
  acknowledgement, snapshot request, snapshot response, and protocol error.
- How the connection epoch is associated with each connection. The server
  must own epoch issuance through `NativeUiSessionCoordinator`; a client
  must not choose or override it.
- Maximum sizes for each message class. The 1024-byte command limit must not
  be assumed to fit a full mixer snapshot.
- A policy for malformed frames, unsupported versions, duplicate requests,
  timeouts, and slow clients.
- Whether snapshot publication is periodic, request-driven, or both, with
  bounded work and no dependency on audio callback timing.

The transport must not forward a command until a complete snapshot for the
active epoch has been accepted and `NativeUiSessionCoordinator::isReady()`
returns true. It must preserve command sequence retry rules and only forward
audio acknowledgements returned for the active epoch.

## Security and operational boundaries

- Use a dedicated directory owned by the intended host process, with the
  narrowest practical permissions.
- Never use a world-writable socket path or accept unauthenticated clients
  by default.
- Consider Linux peer-credential checks only after confirming target support
  and the intended process identities.
- Bound input, output, queued work, and client count.
- A disconnected or malformed client must not stop audio processing.
- No logging, parsing, socket I/O, allocation, or waiting in the audio
  callback.
- Do not change MPC services, startup scripts, ALSA routing, activation,
  licensing, or protected device files as part of transport development.

## Implementation sequence

1. Confirm the target OS/kernel/libc and permitted process deployment model
   through an authorized, non-invasive source.
2. Approve the socket path, ownership, permissions, and process trust model.
3. Specify and test versioned command, snapshot, result, and acknowledgement
   schemas independently of sockets.
4. Implement a bounded framing codec and unit tests on the host. The frame
   codec, partial-I/O adapter, bounded framed-stream channel, and RAII
   descriptor owner are now implemented and covered by host tests.
5. Implement socket creation/bind/listen/accept/connect lifecycle and a
   host-side endpoint behind a narrow interface; test endpoint permissions,
   partial reads/writes, disconnects, malformed frames, and reconnects using
   local test processes.
6. Cross-compile against the confirmed target toolchain.
7. Only then consider device validation, with separate explicit authorization.

An isolated POSIX stream-I/O adapter, bounded framed-stream channel, and
move-only descriptor owner now exist. The channel provides a single-frame
caller-owned transmit queue and an incremental receive pump that preserves
bytes across concatenated frames; the descriptor wrapper adopts an existing
file descriptor and closes it through RAII. None of these components creates,
binds, connects, listens, or accepts sockets, or selects a filesystem path.
No endpoint, device connection, or hardware validation is implemented.
