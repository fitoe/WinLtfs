# WinLtfs

[![License](https://img.shields.io/:license-LGPL%202.1-blue.svg)](https://github.com/rlaphoenix/winltfs/blob/master/LICENSE) ![Platform](https://img.shields.io/badge/platform-Windows%2064--bit-informational) ![LTO](https://img.shields.io/badge/LTO-5%2B-blue) ![LTFS](https://img.shields.io/badge/LTFS-2.4.0-blue) [![WinFsp](https://img.shields.io/badge/WinFsp-2.1-informational)](https://winfsp.dev)

Mount, Format, and Test LTFS tapes on Windows with [WinFsp](https://winfsp.dev).

> [!WARNING]
> It is highly recommended to Copy files instead of Moving files or risk losing your data. While every
> reasonable measure has been taken to prevent data loss, it can still occur in rare cases or with aging
> hardware. Tape drives have an internal memory buffer that stores written data before flushing it to the
> tape. When Moving files to the mount point WinLtfs creates, it gets written to this memory buffer that we
> cannot control. Once moved to the memory buffer, Windows marks the original file for deletion. If the
> Tape drive has an unexpected or intermittent error or failure while its still in the internal memory
> buffer, you may lose your data. Copying instead of moving prevents this issue as you will retain the
> original file on your computer. Only once you unmount the tape should you delete any original files.

## Features

- 🪟 Supports Windows 7 to Windows 11 (64-bit only)
- 📼 LTO-5 to LTO-9 Support including WORM and Type-M
- 💽 Mount LTO tapes as Virtual Drives
- 🗂️ Format, Unformat, and Test LTO tapes
- 💾 Automatic Tape Index Backups
- 🔒 Honors Write-Protection and Read-Only tapes
- 🏷️ Real Cartridge Label shown in Explorer

## Supported Tape Drives

| Brand    | LTO-5 | LTO-6 | LTO-7 | LTO-8 | LTO-9 |
| -------- | :---: | :---: | :---: | :---: | :---: |
| HP / HPE |  ✅   |  ✅   |  ✅   |  ✅   |  ✅   |
| Quantum  |  ✅   |  ✅   |  ✅   |  ✅   |  ✅   |
| Tandberg |  ✅   |  ✅   |  ❓   |  ❓   |  ❓   |
| IBM      |  ❌   |  ❌   |  ❌   |  ❌   |  ❌   |

- ✅: Supported, either whitelisted or has been tested.
- ❓: Uncertain as it needs to be tested to be sure.
- ❌: Not supported as it uses a different tape backend.

Support is matched on the tape drive's SCSI INQUIRY product ID, so any LTO Tape Drive that
reports one of the recognized names should work regardless of brand. The recognized forms are
HP/HPE's `Ultrium N-SCSI`, Quantum's `ULTRIUM N`, and Tandberg's `LTO-N HH` (where `N` is the
LTO generation). You can view your tape drive's reported product ID in Device Manager (its
Properties → Details → *Hardware Ids*) or with any SCSI inquiry tool.

If your LTO Tape Drive is missing from the table, listed incorrectly, or you have tested one
of the uncertain entries, please [open an issue](https://github.com/rlaphoenix/winltfs/issues)
so the list can be updated.

## Background

WinLtfs is a patch of [HPE StoreOpen's LGPL-licensed source code](https://github.com/rlaphoenix/winltfs/blob/hpe/COPYING.LIB), swapping out broken features and code for working modern replacements.

Starting August 2025 and coming into full effect on June 2026, a critical component of the original codebase no longer works on any consumer version of Windows 10 or Windows 11. This is because Microsoft now blocks older drivers that were signed in ways that are no longer trusted ([Source](https://techcommunity.microsoft.com/blog/windows-itpro-blog/advancing-windows-driver-security-removing-trust-for-the-cross-signed-driver-pro/4504818)). The codebase interfaced with UMFSDK.sys, the driver responsible for mounting the LTO Tape Drive. UMFSDK.sys otherwise known as FUSE4Win is very old and seemingly long abandoned, with very little information online.

On July 31, 2026, HPE officially discontinued the HPE StoreOpen Software and removed every single form of download, across all platforms. It would seem they saw it was fully broken (and had been unstable for a very long time), and decided not to continue development, try to fix it or port it to another FUSE-based SDK.

With the help of [Claude](https://claude.ai), the [latest surviving copy of HPE's StoreOpen Source Code](https://github.com/leavelet/ltfs-hp) (v3.5.0) was ported from UMFSDK (FUSE4Win) to [WinFsp](https://winfsp.dev), a native Windows FUSE SDK with very similar capabilities and almost the exact same underlying FUSE2 API. This SDK has a [WHQL-certified driver](https://learn.microsoft.com/en-us/windows-hardware/drivers/install/whql-release-signature), with full Windows 10/11 and Windows Server support, even after Microsoft's driver trust changes.

Thus this project was born, allowing you to continue the use of your LTO Tape Drives on Windows. For most people this project can be considered a drop-in replacement for the entire HPE StoreOpen Suite of LTFS-related Software.

> [!NOTE]
> The last released version of HPE StoreOpen was v3.6.0, but its source code has been lost
> — no download, mirror, or archive has been found — so it is unknown what changes were made
> in it.

## Comparison to HPE StoreOpen

HPE's LTT (Library & Tape Tools) is still recommended. The following features from HPE StoreOpen are not yet implemented:

- 'Offline' mode for mounted files to disable thumbnails for better performance
  (as WinFsp does not seem to support it)
- Possibly missing obscure features I don't know about that are not advertised
  in the HPE StoreOpen GUIs...

And finally, mounting works slightly differently in this project. You must have a
valid tape cartridge inserted with LTFS format to be able to mount. In HPE's software
it lets you mount more-so the 'tape drive' instead of the tape cartridge, so you could
eject, re-insert, or change cartridge all without having to unmount/remount the tape
drive. Unfortunately, this is not seemingly possible with WinFsp, at least I can't
figure out a clean reliable way to.

## Usage

WinLtfs ships four command-line tools. Each accepts `-h`/`--help` (and `-p`/`--advanced-help`
where noted) to print the option reference reproduced below. On Windows you pass a drive letter as the `ltfs`
mountpoint and point `-i`/`config_file` at your `ltfs.conf`.

Every mount and unmount automatically backs up the tape cartridge's index partition as an XML
file to `C:\tmp\ltfs\<barcode-or-uuid>.schema`. It is highly recommended to keep these and back
them up regularly in case of tape failure. These files will be crucial in case of any data
recovery attempts. They are also very handy to go back and figure out what files each tape
cartridge holds.

### `ltfs.exe`

Mounts an LTFS-formatted tape cartridge as a filesystem (on Windows, a drive letter). The
generic FUSE options are omitted below; run with `-a` for the full advanced help.

```powershell
# Mount the tape in drive TAPE0 as Windows drive T:
ltfs.exe T: -o config_file=ltfs.conf -o devname=TAPE0
```

```text
usage: ltfs mountpoint [options]

general options:
    -o opt,[opt...]           mount options
    -h   --help               print help
    -V   --version            print version

LTFS options:
    -o config_file=<file>     Configuration file (default: /usr/local/etc/ltfs.conf)
    -o work_directory=<dir>   LTFS work directory (default: /tmp/ltfs)
    -o atime                  Update index if only access times have changed
    -o noatime                Do not update index if only access times have changed (default)
    -o tape_backend=<name>    tape backend to use (default: ltotape)
    -o iosched_backend=<name> I/O scheduler to use (default: unified, "none" to disable)
    -o umask=<mode>           Override default permission mask (3 octal digits, default: 000)
    -o fmask=<mode>           Override file permission mask (3 octal digits, default: 000)
    -o dmask=<mode>           Override directory permission mask (3 octal digits, default: 000)
    -o min_pool_size=<num>    Minimum write cache pool size, 1 MB objects (default: 25)
    -o max_pool_size=<num>    Maximum write cache pool size, 1 MB objects (default: 50)
    -o rules=<rules>          Rules for choosing files to write to the index partition
                              (e.g. size=1M, size=1M/name=*.jpg:*.png)
    -o quiet                  Disable informational messages
    -o trace                  Enable diagnostic output
    -o syslogtrace            Enable diagnostic output to stderr and syslog
    -o fulltrace              Enable full call tracing
    -o verbose=<num>          Override output verbosity directly (default: 2)
    -o eject                  Eject the cartridge after unmount
    -o noeject                Do not eject the cartridge after unmount (default)
    -o sync_type=<type>       Sync type: time@<min>, close, or unmount (default: time@5)
    -o force_mount_no_eod     Skip EOD existence check when mounting (read-only)
    -o rollback_mount=<gen>   Mount a previous index generation (read-only)
    -o release_device         Clear device reservation (use with -o devname)
    -o capture_index          Capture latest index to the work directory at unmount
    -a                        Advanced help, including standard FUSE options

LTOTAPE backend options:
    -o devname=<dev>          tape device (default=/dev/nst0)
    -o log_directory=<dir>    log snapshot directory (default=/var/log)
    -o nosizelimit            remove 512kB limit (NOT RECOMMENDED)
```

### `mkltfs.exe`

Formats a tape cartridge with the LTFS format.

```powershell
# Format the tape in drive TAPE0 (optional 6-char serial, custom volume name)
mkltfs.exe -i ltfs.conf -d TAPE0 -s ABCDEF -n "My LTFS Tape"
```

```text
Usage: mkltfs <options>

Available options are:
  -d, --device=<name>       Tape device (required)
  -f, --force               Force to format medium
  -s, --tape-serial=<id>    Tape serial number (6 alphanumeric ASCII characters)
  -n, --volume-name=<name>  Tape volume name (LTFS VOLUME by default)
  -r, --rules=<rules>       Rules for choosing files to write to the index partition
                            (e.g. size=1M, size=1M/name=*.jpg:*.png). Size accepts
                            K, M, G suffixes; names may use '?' and '*'.
      --no-override         Disallow mount-time data placement policy changes
  -w, --wipe                Restore the medium to an unpartitioned (legacy scratch) medium
  -q, --quiet               Suppress progress information and general messages
  -t, --trace               Enable function call tracing
      --syslogtrace         Enable diagnostic output to stderr and syslog
  -V, --version             Version information
  -h, --help                This help
  -p, --advanced-help       Full help, including advanced options
  -g, --interactive         Interactive mode
  -i, --config=<file>       Use the specified configuration file
  -e, --backend=<name>      Use the specified tape device backend (default: ltotape)
  -b, --blocksize=<num>     Set the LTFS record size (default: 524288)
  -c, --no-compression      Disable compression on the volume
  -k, --keep-capacity       Keep the tape medium's total capacity proportion
  -x, --fulltrace           Enable full function call tracing (slow)
      --long-wipe           Unformat and erase all data by overwriting (takes 3+ hours,
                            cannot be interrupted)
```

### `ltfsck.exe`

Checks and repairs an LTFS volume, and can list or roll back to previous index generations.

```powershell
# Check and repair the LTFS volume in drive TAPE0
ltfsck.exe -i ltfs.conf TAPE0
```

```text
Usage: ltfsck [options] filesys

  filesys                         Device file for the tape drive

Available options are:
  -g, --generation=<generation>   Specify the generation to roll back
  -r, --rollback                  Roll back to the point specified by -g
  -n, --no-rollback               Do not roll back; verify the point specified by -g (default)
  -f, --full-recovery             Recover extra data blocks into _ltfs_lostandfound
  -z, --deep-recovery             Recover a cartridge with missing EOD
  -l, --list-rollback-points      List rollback points
  -m, --full-index-info           Display full index information (with -l only)
  -v, --traverse=<strategy>       Traverse mode for listing rollback points:
                                  forward or backward (default: backward)
  -j, --erase-history             Erase history at rollback
  -k, --keep-history              Keep history at rollback (default)
  -q, --quiet                     Suppress informational messages
  -t, --trace                     Enable diagnostic output
      --syslogtrace               Enable diagnostic output to stderr and syslog
  -V, --version                   Version information
  -h, --help                      This help
  -p, --advanced-help             Full help, including advanced options
  -i, --config=<file>             Use the specified configuration file
  -e, --backend=<name>            Override the default tape device backend
  -x, --fulltrace                 Enable full function call tracing (slow)
      --capture-index             Capture index information to the current directory
      --salvage-rollback-points   List rollback points of a cartridge that has no EOD
```

### `unltfs.exe`

Removes the LTFS format from a cartridge (unformat). This IRRETRIEVABLY DESTROYS all
contents of the cartridge.

```powershell
# Remove the LTFS format from the tape in drive TAPE0 (destroys its contents)
unltfs.exe -i ltfs.conf -d TAPE0
```

```text
Usage: unltfs <options>

  -d, --device=<name> specifies the tape drive to use
  -y, --justdoit      omit normal verification steps, reformat without prompting
  -e, --eject         eject tape after operation completes successfully
  -q, --quiet         suppress all progress output
  -t, --trace         display detailed progress
  -h, --help          shows this help
  -i, --config=<file> override the default config file
  -b, --backend       specify a different tape backend subsystem
  -x, --fulltrace     display debug information (verbose)
```

## Reading tape attributes

A mounted tape exposes read-only information about the volume, the cartridge and
its files. Nothing here writes to the tape or moves it, and values are read live,
so a swapped tape is never reported with the old tape's values. Attribute IDs are
fixed and never reused; the full list lives in
[`ltog_attributes.h`](ltfs/src/libltfs/ltog_attributes.h).

### Method 1 (Extended attributes)

The volume root lists these as extended attributes (EAs). EA caching is off, so
they always reflect the tape currently loaded. List them for a tape mounted on `T:`:

```powershell
fsutil file queryEA T:\
```

They can also be read one at a time with [DeviceIoControl](#method-2-deviceiocontrol) using
the IDs below.

| ID | Attribute | Description |
| --- | --- | --- |
| `0x800` | `ltfs.volumeSerial` | Cartridge serial (volser) |
| `0x801` | `ltfs.volumeUUID` | Unique ID of this LTFS volume |
| `0x802` | `ltfs.volumeName` | Volume name set at format time |
| `0x803` | `ltfs.volumeFormatTime` | When the tape was formatted |
| `0x804` | `ltfs.volumeBlocksize` | Block size in bytes |
| `0x805` | `ltfs.volumeCompression` | `true` if compression is enabled |
| `0x806` | `ltfs.labelVersion` | LTFS label format version |
| `0x807` | `ltfs.labelCreator` | Software that formatted the tape |
| `0x808` | `ltfs.partitionMap` | Which partition holds the index and which holds the data |
| `0x809` | `ltfs.indexGeneration` | Index generation number (increases on every update) |
| `0x80a` | `ltfs.indexTime` | When the current index was written |
| `0x80b` | `ltfs.indexVersion` | LTFS index format version |
| `0x80c` | `ltfs.indexLocation` | Tape position of the current index |
| `0x80d` | `ltfs.indexPrevious` | Tape position of the previous index |
| `0x80e` | `ltfs.indexCreator` | Software that wrote the current index |
| `0x80f` | `ltfs.commitMessage` | Commit message stored with the index |
| `0x810` | `ltfs.policyExists` | `true` if a data placement policy is set |
| `0x811` | `ltfs.policyAllowUpdate` | `true` if that policy may be changed |
| `0x812` | `ltfs.policyMaxFileSize` | Largest file the policy keeps on the index partition, in bytes |
| `0x813` | `ltfs.softwareVersion` | Version of this LTFS engine |
| `0x814` | `ltfs.softwareFormatSpec` | LTFS format spec the engine implements |
| `0x815` | `ltfs.softwareVendor` | Engine vendor |
| `0x816` | `ltfs.softwareProduct` | Engine product name |
| `0x817` | `ltfs.mamBarcode` | Barcode stored in cartridge memory |
| `0x818` | `ltfs.mamVolumeName` | Volume name stored in cartridge memory |
| `0x819` | `ltfs.mamApplicationVendor` | Vendor of the software that last wrote the tape |
| `0x81a` | `ltfs.mamApplicationName` | Name of that software |
| `0x81b` | `ltfs.mamApplicationVersion` | Version of that software |
| `0x81c` | `ltfs.mamApplicationFormatVersion` | LTFS format version it wrote |

### Method 2 (DeviceIoControl)

These ask the drive or describe individual files, so they are not listed as EAs.
Every EA above can also be read this way, using its ID.

IDs `0x833` to `0x83a` work on any file or folder; the rest need the volume root.

| ID | Attribute | Description |
| --- | --- | --- |
| `0x81d` | `ltfs.mediaDataPartitionTotalCapacity` | Data partition size, MiB |
| `0x81e` | `ltfs.mediaDataPartitionAvailableSpace` | Free space on the data partition, MiB |
| `0x81f` | `ltfs.mediaIndexPartitionTotalCapacity` | Index partition size, MiB |
| `0x820` | `ltfs.mediaIndexPartitionAvailableSpace` | Free space on the index partition, MiB |
| `0x821` | `ltfs.mediaLoads` | Times the cartridge has been loaded |
| `0x822` | `ltfs.mediaRecoveredWriteErrors` | Write errors the drive recovered from |
| `0x823` | `ltfs.mediaPermanentWriteErrors` | Write errors it could not recover |
| `0x824` | `ltfs.mediaRecoveredReadErrors` | Read errors the drive recovered from |
| `0x825` | `ltfs.mediaPermanentReadErrors` | Read errors it could not recover |
| `0x826` | `ltfs.mediaPreviousPermanentWriteErrors` | Unrecovered write errors from the previous load |
| `0x827` | `ltfs.mediaPreviousPermanentReadErrors` | Unrecovered read errors from the previous load |
| `0x828` | `ltfs.mediaBeginningMediumPasses` | Passes over the start of the tape |
| `0x829` | `ltfs.mediaMiddleMediumPasses` | Passes over the middle of the tape |
| `0x82a` | `ltfs.mediaEfficiency` | Drive-reported tape efficiency |
| `0x82b` | `ltfs.mediaDatasetsWritten` | Datasets written over the cartridge's lifetime |
| `0x82c` | `ltfs.mediaDatasetsRead` | Datasets read over its lifetime |
| `0x82d` | `ltfs.mediaMBWritten` | Megabytes written over its lifetime |
| `0x82e` | `ltfs.mediaMBRead` | Megabytes read over its lifetime |
| `0x82f` | `ltfs.mediaStorageAlert` | TapeAlert flags reported by the drive |
| `0x830` | `ltfs.mediaEncrypted` | `true` if the cartridge holds encrypted data |
| `0x831` | `ltfs.driveEncryptionState` | Drive encryption: `on`, `off` or `unknown` |
| `0x832` | `ltfs.driveEncryptionMethod` | Drive encryption method |
| `0x833` | `ltfs.fileUID` | Unique file ID within the volume |
| `0x834` | `ltfs.createTime` | Creation time |
| `0x835` | `ltfs.modifyTime` | Last content change |
| `0x836` | `ltfs.accessTime` | Last access |
| `0x837` | `ltfs.changeTime` | Last metadata change |
| `0x838` | `ltfs.backupTime` | Backup time |
| `0x839` | `ltfs.partition` | Partition holding the file's data |
| `0x83a` | `ltfs.startblock` | First tape block of the file's data |
| `0x83b` | Raw MAM | Lists and reads *every* attribute in cartridge memory, including vendor-specific ones. See [Reading raw MAM](#reading-raw-mam) |

**Example in C/C++ (reading Volume Serial)**

```c
#include <windows.h>

HANDLE h = CreateFileW(L"T:\\", FILE_READ_EA,
    FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, NULL,
    OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, NULL);
unsigned char reply[4096];
DWORD got;
/* control code = (0xC657 << 16) | (ID << 2); no input, 4096-byte output */
DeviceIoControl(h, (0xC657u << 16) | (0x800u << 2), NULL, 0,
    reply, sizeof(reply), &got, NULL);
CloseHandle(h);
/* status at reply+8, length at reply+12, serial text at reply+64 */
```

**Reply format**

| Offset | Type | Field |
| --- | --- | --- |
| 0 | uint32 | Magic `0x474F544C` ("LTOG") |
| 4 | uint32 | Reply version: `1` |
| 8 | int32 | Status: `0` on success, otherwise a negative LTFS error |
| 12 | uint32 | Value length in bytes |
| 16 | char[40] | UUID of the volume that answered |
| 56 | 8 bytes | Reserved |
| 64 | char[4032] | The value as UTF-8 text (not NUL-terminated; use the length) |

All integers are little-endian. A non-zero status means the value could not be read; don't treat it as `0`.
When reading several attributes, check that every reply carries the same volume
UUID. If it changed, the tape was swapped and you should start again.

### Reading raw MAM

Command `0x83b` reads cartridge memory directly in two steps: list the attribute
IDs present, then read each one. Use control code `(0xC657 << 16) | (0x83B << 2)`
with **4096-byte input and output buffers**. Zero the input buffer, then fill in:

| Offset | Type | Field |
| --- | --- | --- |
| 0 | uint32 | Version: `1` |
| 4 | uint8 | Operation: `0` = list IDs, `1` = read one attribute |
| 5 | uint8 | Physical partition: `0` or `1` |
| 6 | uint16 | Attribute ID to read (`0` when listing) |
| 8 | uint32 | Byte offset to start from (`0` for the first page) |
| 12 | uint32 | Reserved: `0` |

The reply uses the reply format above with version `2`, and its reserved bytes carry
paging information:

| Offset | Type | Field |
| --- | --- | --- |
| 56 | uint32 | Total result size in bytes |
| 60 | uint32 | Offset this page starts at |

A result can be larger than one 4032-byte page. Repeat the request, adding the
returned length to the offset, until you have the total.

- **List result:** a sequence of 2-byte big-endian attribute IDs.
- **Read result:** the attribute exactly as the drive stores it:

| Bytes | Meaning |
| --- | --- |
| 0–1 | Attribute ID (big-endian) |
| 2 | Bit 7 set = read-only; bits 0–1 = format (`0` binary, `1` ASCII, `2` text) |
| 3–4 | Value length (big-endian) |
| 5 onward | The value |

The engine and every tape backend DLL must come from the same build, because
raw MAM support adds a function to the backend interface.

## Testing

You can test the ltfs executables without a real tape drive using the file-based backend, a virtualized tape drive that stores its data as files.

```powershell
mkdir faketape  # create folder
.\mkltfs.exe -i .\ltfs.conf -e file -d C:/path/to/faketape -s TEST01 -n DEMO  # make a 'fake' tape drive inside
.\ltfs.exe T: -f -o config_file=C:/path/to/dist/ltfs.conf `  # mount it
    -o tape_backend=file -o devname=C:/path/to/faketape -o sync_type=close
```

## Development

### Pre-requisites

1. **MSYS2**: <https://www.msys2.org>.
2. **WinFsp** with the **Developer** feature: <https://winfsp.dev>.

### Building

From the repo root in a MINGW64 shell:

```sh
./setup.sh    # one-time: deps, WinFsp staging, autoreconf + configure
./build.sh    # compile everything and stage dist/
```

`setup.sh` is only needed once (and again after a `git clean` or a pull that
touches the autotools inputs). `build.sh` can be re-run freely, and takes an
optional target:

| Command                        | What it does                               |
| ------------------------------ | ------------------------------------------ |
| `./build.sh`           | `make` + filedebug backend + stage `dist/` |
| `./build.sh make`      | just compile the LTFS tree                 |
| `./build.sh filedebug` | just the file-emulator tape backend        |
| `./build.sh dist`      | just (re)stage `dist/` from what is built  |
| `./build.sh clean`     | `make clean`                               |

### Output

`dist/` is a self-contained binary set:

```
dist/
├── ltfs.exe                     [mount an LTFS tape as a Windows drive (the FUSE filesystem)]
├── mkltfs.exe                   [format a tape as LTFS]
├── ltfsck.exe                   [check and repair an LTFS volume]
├── unltfs.exe                   [remove the LTFS format and reclaim a tape]
├── libltfs.dll                  [core LTFS library the tools and plugins link against]
├── libdriver-ltotape-win.dll    [tape backend: real LTO drives over Windows SCSI]
├── libdriver-file.dll           [tape backend: file emulator (test with no hardware)]
├── libiosched-unified.dll       [I/O scheduler: unified (default)]
├── libiosched-fcfs.dll          [I/O scheduler: first-come, first-served]
├── libkmi-flatfile.dll          [key-manager interface: flat-file key store]
├── libkmi-simple.dll            [key-manager interface: simple key store]
├── winfsp-x64.dll               [WinFsp runtime that provides the FUSE layer]
├── (message catalog DLLs)       [ICU-based localized LTFS message resources]
├── (MinGW-w64 runtime DLLs)     [gcc, winpthread, libxml2, ICU, zlib — pulled in via ldd]
├── ltfs.conf                    [generated plugin registry pointing at the DLLs above]
├── LICENSE                      [LTFS engine license (LGPL v2.1)]
└── licenses/                    [third-party runtime component license texts]
```

## Credit

This project stands almost entirely on other people's work:

- **IBM** - the original Linear Tape File System Single Drive Edition;
  `libltfs` and the core utilities are IBM Almaden Research code.
- **Hewlett-Packard / HPE** - the Windows port (StoreOpen 3.5.0) and the
  `ltotape` drive backend for HP LTO drives.
- **OSR Open Systems Resources, Inc.** - the original Windows FUSE
  integration work inside the HP tree.
- **nix-community** - for preserving the HPE StoreOpen 3.4.2 source code
  ([nix-community/hpe-ltfs](https://github.com/nix-community/hpe-ltfs)).
- **leavelet** - for preserving the HPE StoreOpen 3.5.0 source code
  ([leavelet/ltfs-hp](https://github.com/leavelet/ltfs-hp)).
- **Bill Zissimopoulos** - [WinFsp](https://winfsp.dev), whose excellent
  FUSE-compatible layer and properly signed driver make this whole approach
  possible.
- **Claude** - Assistance with porting and orchestration.

## Licensing

WinLtfs as a whole is distributed under the **GNU Lesser General Public License v2.1**
(see [LICENSE](LICENSE)) — the same license as the upstream LTFS source it patches, so the
result stays upstreamable.

The per-component license texts for everything bundled into a `dist/` build live in the
[`licenses/`](licenses/) folder (set up a little differently to HPE's original layout).
`scripts/build.sh` copies them alongside the binaries. In summary:

- The LTFS source and utilities (`libltfs`, `ltfs`, `mkltfs`, `ltfsck`, `unltfs`) are
  **LGPL-2.1**, © IBM, HP/HPE, and OSR. The WinFsp port patches modify that code and are
  therefore offered under **LGPL-2.1** as well, so they remain upstreamable.
- **WinFsp** is GPLv3 with a FLOSS exception, © Bill Zissimopoulos. Its redistributable
  `winfsp-x64.dll` is shipped in `dist/`; the FLOSS exception is what lets the LGPL-2.1 LTFS
  binaries link the WinFsp FUSE layer. Source: <https://github.com/winfsp/winfsp> (tag `v2.1`).
- The MSYS2-built runtime DLLs bundled in `dist/` carry their own licenses: libxml2 (MIT),
  ICU (Unicode), GNU libiconv (LGPL-2.1), zlib (Zlib), MinGW-w64 winpthreads (MIT/BSD), and
  the GCC runtime `libgcc`/`libstdc++` (GPL-3.0 with the GCC Runtime Library Exception).

---

© rlaphoenix 2026
