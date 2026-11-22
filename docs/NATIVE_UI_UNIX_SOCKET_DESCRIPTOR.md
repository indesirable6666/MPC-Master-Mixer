# Native UI Unix socket descriptor owner

`NativeUiUnixSocketDescriptor` is a move-only RAII owner for an already
created POSIX file descriptor. It supports ownership transfer, explicit
release, reset/close, and setting nonblocking and close-on-exec flags.

It deliberately does not call `socket`, `bind`, `listen`, `accept`, or
`connect`. The caller must obtain a descriptor through an independently
approved endpoint lifecycle and then transfer ownership into this wrapper.
The wrapper does not validate peer identity or filesystem path permissions.

The descriptor is closed on destruction/reset. Do not retry `close()` after
an error such as `EINTR`: descriptor state is platform-dependent and retrying
can accidentally close a reused descriptor. Destruction and flag changes
belong on a control/transport thread, never an audio callback.

Host tests use a local `AF_UNIX` socketpair to check move semantics, ownership
release, close behavior, nonblocking mode, and close-on-exec. This is not a
device or MPC service test.
