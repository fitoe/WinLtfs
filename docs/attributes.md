# Read-only Windows attribute queries

Root EA enumeration includes 29 inexpensive LTFS metadata attributes, including
`ltfs.volumeSerial` and `ltfs.volumeUUID`. Values come from the existing LTFS
getters. Capacity, health, encryption, and file attributes are queried explicitly
so ordinary identity lookup does not invoke hardware diagnostics.

## Control interface

The single attribute map is `ltfs/src/libltfs/ltog_attributes.h`. Each entry gives
a stable command ID, name, root-only flag, and EA-enumeration flag. Never renumber
or reuse existing IDs. This interface currently has no discovery command.

Open the mounted volume root (or a file for file attributes) with `CreateFileW`,
`FILE_READ_EA`, shared read/write/delete access, `OPEN_EXISTING`, and
`FILE_FLAG_BACKUP_SEMANTICS`. Use `FILE_FLAG_OPEN_REPARSE_POINT` when the query
must apply to the entry itself rather than following its reparse target.

Call `DeviceIoControl` with control code `(0xC657u << 16) | (id << 2)`, no input,
and exactly 4096 bytes of output. WinFsp maps this to
`FSP_FUSE_IOCTL(id, 0, 4096)`. Unknown commands, input-bearing requests, different
output sizes, and unsupported FUSE flags are rejected.

The response uses little-endian integers:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | uint32 | Magic `0x474f544c` (LTOG, retained for client compatibility) |
| 4 | uint32 | Protocol version, currently 1 |
| 8 | int32 | Raw LTFS getter status: 0 or a negative LTFS error |
| 12 | uint32 | Value length, at most 4032 bytes |
| 16 | char[40] | Volume UUID, NUL-padded |
| 56 | byte[8] | Reserved, zero |
| 64 | byte[4032] | UTF-8 value; use the length, not NUL termination |

A successful control request can carry a getter error. Do not treat missing,
unsupported, empty, or `unknown` values as numeric zero. Oversized values return
the getter's buffer error rather than a truncated value. Capacity attributes use
MiB; volume block size and policy maximum file size use bytes.

The engine checks readiness using the mounted device before and after the getter.
On transport or revalidation failure, discard the response. For multiple queries,
keep the same handle and discard the report if UUIDs differ. Values are not an
atomic snapshot of changes within the same volume.

## Scope

The allowlist exposes existing getters only. It does not accept arbitrary names,
setters, sync requests, or explicit dump operations. `ltfs.volumeLockState` is
excluded because its existing getter has side effects. Diagnostic getters use
the existing device locking; encryption queries also acquire this lock.

Some hardware backends capture diagnostic dumps automatically after a failed
read command. This was observed for encryption MODE SENSE queries on an HP LTO6
drive. Ordinary identity/metadata EA queries do not invoke those getters.

The interface operates on an existing mount and does not open another tape
device handle. Clients normally do not need elevation if the mounted filesystem
permissions allow access.
