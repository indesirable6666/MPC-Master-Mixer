# Native UI Unix socket path policy

`NativeUiUnixSocketPathPolicy` performs pure, allocation-free-at-the-interface
path checks before a future filesystem Unix-domain socket endpoint is
considered. It requires an absolute non-root path, rejects trailing slashes,
embedded NUL bytes, overlong `sun_path` values, and explicit `.` or `..`
components. Repeated separators are accepted and are not rewritten.

This is deliberately not an endpoint implementation. It does not call
`socket`, `bind`, `listen`, `accept`, or `connect`; it does not inspect
directory ownership, permissions, existing files, symlinks, or peer identity.
It cannot prevent filesystem races. A future endpoint must separately use an
approved dedicated directory, inspect and securely own the created socket
inode, set restrictive permissions, and handle cleanup without unlinking a
replacement path.

The target MPC kernel/libc, deployment directory, process identities, and
permission model remain unverified. This policy is host-tested and
cross-compiled only; it does not authorize selecting a device path or running
an endpoint on the MPC.
