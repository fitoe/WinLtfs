# Read-only Windows attribute queries

Root EA enumeration includes 29 inexpensive LTFS metadata attributes, including
`ltfs.volumeSerial` and `ltfs.volumeUUID`. Values come from the existing LTFS
getters. Capacity, health, encryption, and file attributes are queried explicitly
so ordinary identity lookup does not invoke hardware diagnostics.

## Control interface

The single attribute map is `ltfs/src/libltfs/ltog_attributes.h`. Each entry gives
a stable command ID, name, root-only flag, and EA-enumeration flag. Never renumber
or reuse existing IDs. These 59 named queries retain their original protocol.
The separate MAM command below discovers the attributes present on the medium.

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

The named allowlist exposes existing getters only. It does not accept arbitrary
names, setters, sync requests, or explicit dump operations. `ltfs.volumeLockState` is
excluded because its existing getter has side effects. Diagnostic getters use
the existing device locking; encryption queries also acquire this lock.

Some hardware backends capture diagnostic dumps automatically after a failed
read command. This was observed for encryption MODE SENSE queries on an HP LTO6
drive. Ordinary identity/metadata EA queries do not invoke those getters.

The interface operates on an existing mount and does not open another tape
device handle. Clients normally do not need elevation if the mounted filesystem
permissions allow access.

## Complete MAM discovery and raw values

Command `0x83b` queries every available MAM attribute, including vendor-specific
IDs, on physical partition 0 or 1. It uses READ ATTRIBUTE service action 1 to
enumerate available IDs and service action 0 to read values. It is root-only,
read-only, and is not part of automatic EA enumeration. No WRITE ATTRIBUTE,
lock-state setter, or tape positioning command is used. An unavailable attribute
or unsupported command returns an error, never a fabricated zero or empty value.

This includes the six named MAM attributes plus all other device-reported IDs,
such as VCR (`0x0009`), text localization (`0x0805`), coherency (`0x080C`), HPE
EWSTATE (`0x1500`), and volume lock state (`0x1623`), where available. The latter
is the raw MAM byte, not the derived `ltfs.volumeLockState` getter with side effects.
Unknown IDs are preserved without needing a new application release.

Use control code `(0xC657u << 16) | (0x83bu << 2)` with **4096 bytes of input and
4096 bytes of output**. WinFsp requires equal sizes for bidirectional control
requests. Initialize all input bytes to zero, then fill this little-endian header:

| Offset | Type | Meaning |
| --- | --- | --- |
| 0 | uint32 | Request version: 1 |
| 4 | uint8 | Operation: 0 = enumerate IDs, 1 = read attribute |
| 5 | uint8 | Physical partition: 0 or 1 (not LTFS `a`/`b`) |
| 6 | uint16 | MAM attribute ID; must be zero for enumeration |
| 8 | uint32 | Byte offset into the result; start at zero |
| 12 | uint32 | Reserved, zero |
| 16 | byte[4080] | Reserved, zero |

The response has the same 64-byte header as the named queries, with version **2**.
Offset 56 contains a uint32 total payload length; offset 60 contains the echoed
uint32 requested byte offset. Offset 12 is the returned chunk length (up to 4032).
On getter error, payload, lengths and offset are zero; status preserves the error.

Enumeration payload is a sequence of **big-endian uint16 IDs**. Read each listed
ID separately. Attribute payload is its original SCSI descriptor:

| Offset in payload | Meaning |
| --- | --- |
| 0–1 | Big-endian attribute ID |
| 2 | Bit 7: read-only; bits 1–0: format (0 binary, 1 ASCII, 2 text) |
| 3–4 | Big-endian value length |
| 5 onward | Unmodified value bytes |

Binary values are not UTF-8 strings. Text encoding is identified by MAM attribute
`0x0805`. Preserve raw bytes for unknown formats/IDs. Empty values still have a
five-byte descriptor and differ from missing attributes. Values of up to 65535
bytes and ID lists containing all 65536 possible IDs are supported. Advance the
offset by the returned length until it equals total length; an offset past the
end is rejected. Each page rereads the device; this is not an atomic snapshot.
Discard the whole report on transport failure or UUID changes, and retry if
lengths or the enumerated list change. Values can still change without changing
their lengths while a mounted volume is in use.

Export both partitions as JSON with Python 3 on Windows:

```powershell
python tools/mam_dump.py T:\ --output mam.json
```

The export includes ID, raw hex, format, read-only flag, and per-attribute errors;
ASCII values also have a text field. Exit status is 0 for a complete report,
2 for a report with getter errors, and 1 for transport/protocol failure. It does
not write a new report after transport/protocol failure. Actual available IDs
depend on the cartridge and drive; this interface covers accessible MAM data,
not hidden firmware data or unsupported attributes.

The tape backend ABI adds `read_mam`: rebuild and distribute the engine and
backend DLLs together. Both the Windows tape backend and file emulator implement
it. Do not combine this engine with older or unrecompiled third-party backends.

## Validation

In an MSYS2 MINGW64 shell, run the protocol boundary tests:

```sh
gcc -Wall -Wextra -Werror -Iltfs/src/libltfs tests/mam_payload_test.c -o build/mam_payload_test.exe
./build/mam_payload_test.exe
```

After building/staging the engine and file backend, run on Windows:

```powershell
python tests/mam_integration.py build/mam-dist
```

The integration test creates a new file-emulator tape and chooses an unused
drive letter. It checks ID-list pagination, both partitions, a 65535-byte value,
empty/missing/truncated attributes, invalid requests, JSON export and the
unchanged version-1 interface. It stops only its own emulator process and retains
fixtures/logs under `build/`. Real-drive MAM compatibility needs hardware validation.
