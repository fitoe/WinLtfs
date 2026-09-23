"""Export every available MAM attribute from a mounted WinLTFS root (Windows)."""
import argparse
import ctypes
from ctypes import wintypes
import json
import os
import struct
import sys


class GetterError(Exception):
    def __init__(self, status):
        self.status = status
        super().__init__(f"LTFS status {status}")


class MountedMAM:
    def __init__(self, root):
        self.api = ctypes.WinDLL("kernel32", use_last_error=True)
        self.api.CreateFileW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD,
            wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD,
            wintypes.HANDLE]
        self.api.CreateFileW.restype = wintypes.HANDLE
        self.api.DeviceIoControl.argtypes = [wintypes.HANDLE, wintypes.DWORD,
            ctypes.c_void_p, wintypes.DWORD, ctypes.c_void_p, wintypes.DWORD,
            ctypes.POINTER(wintypes.DWORD), ctypes.c_void_p]
        self.api.DeviceIoControl.restype = wintypes.BOOL
        self.api.CloseHandle.argtypes = [wintypes.HANDLE]
        self.api.CloseHandle.restype = wintypes.BOOL
        # FILE_READ_EA, share read/write/delete, OPEN_EXISTING, backup semantics.
        self.handle = self.api.CreateFileW(root, 8, 7, None, 3, 0x02000000, None)
        if self.handle == ctypes.c_void_p(-1).value:
            raise ctypes.WinError(ctypes.get_last_error())
        self.uuid = None

    def close(self):
        self.api.CloseHandle(self.handle)

    def read(self, partition, operation, attribute=0):
        result = bytearray()
        expected_total = None
        while True:
            request = struct.pack("<IBBHII", 1, operation, partition, attribute,
                                  len(result), 0)
            source = ctypes.create_string_buffer(request, 4096)
            output = ctypes.create_string_buffer(4096)
            transferred = wintypes.DWORD()
            code = (0xC657 << 16) | (0x83b << 2)
            if not self.api.DeviceIoControl(self.handle, code, source, 4096,
                    output, 4096, ctypes.byref(transferred), None):
                raise ctypes.WinError(ctypes.get_last_error())
            if transferred.value != 4096:
                raise ValueError("Incomplete MAM response")
            raw = output.raw
            magic, version, status, length = struct.unpack_from("<IIiI", raw)
            uuid = raw[16:56].split(b"\0", 1)[0].decode("ascii")
            total, offset = struct.unpack_from("<II", raw, 56)
            if magic != 0x474F544C or version != 2 or not uuid:
                raise ValueError("Invalid MAM response header")
            if self.uuid is not None and self.uuid != uuid:
                raise ValueError("Volume changed; discard this export")
            self.uuid = uuid
            if status:
                raise GetterError(status)
            if (length > 4032 or total > 131072 or offset != len(result)
                    or offset + length > total
                    or (expected_total is not None and total != expected_total)):
                raise ValueError("Invalid or changing MAM page")
            expected_total = total
            result.extend(raw[64:64 + length])
            if len(result) == total:
                return bytes(result)
            if not length:
                raise ValueError("MAM pagination made no progress")


def export(reader):
    report = {"volume_uuid": None, "partitions": [], "complete": True}
    for partition in (0, 1):
        section = {"partition": partition, "attributes": []}
        report["partitions"].append(section)
        try:
            ids = reader.read(partition, 0)
        except GetterError as error:
            section["status"] = error.status
            report["complete"] = False
            continue
        if len(ids) % 2:
            raise ValueError("Malformed MAM attribute list")
        for (attribute,) in struct.iter_unpack(">H", ids):
            entry = {"id": f"0x{attribute:04X}"}
            section["attributes"].append(entry)
            try:
                descriptor = reader.read(partition, 1, attribute)
            except GetterError as error:
                entry["status"] = error.status
                report["complete"] = False
                continue
            if len(descriptor) < 5:
                raise ValueError("Truncated MAM descriptor")
            returned_id, flags, length = struct.unpack_from(">HBH", descriptor)
            if returned_id != attribute or len(descriptor) != length + 5:
                raise ValueError("Malformed MAM descriptor")
            value = descriptor[5:]
            entry.update(status=0, format=flags & 3, read_only=bool(flags & 0x80),
                         flags=flags, length=length, hex=value.hex())
            if flags & 3 == 1:
                # ASCII only; TEXT encoding is specified by MAM attribute 0x0805.
                entry["text"] = value.decode("ascii", errors="replace").rstrip(" \0")
        if reader.read(partition, 0) != ids:
            raise ValueError("MAM attribute list changed; retry the export")
    report["volume_uuid"] = reader.uuid
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", help="Mounted volume root, e.g. T:\\")
    parser.add_argument("--output", help="Write UTF-8 JSON to this file")
    args = parser.parse_args()
    if os.name != "nt":
        parser.error("This tool requires Windows")
    root = os.path.abspath(args.root)
    if len(root) == 2 and root[1] == ":":
        root += "\\"
    reader = MountedMAM(root)
    try:
        report = export(reader)
    finally:
        reader.close()
    data = json.dumps(report, ensure_ascii=False, indent=2)
    if args.output:
        with open(args.output, "w", encoding="utf-8") as target:
            target.write(data + "\n")
    else:
        print(data)
    return 0 if report["complete"] else 2


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (OSError, ValueError, GetterError) as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
