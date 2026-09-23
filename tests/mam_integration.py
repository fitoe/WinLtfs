"""Windows integration test using a NEW file-emulator tape, never tape hardware.

Run: python tests/mam_integration.py build/mam-dist
Fixtures and logs are retained under build/ for inspection.
"""
import ctypes
from ctypes import wintypes
import importlib.util
from pathlib import Path
import struct
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location("mam_dump", ROOT / "tools/mam_dump.py")
mam = importlib.util.module_from_spec(spec)
spec.loader.exec_module(mam)


def control(reader, command, source, size=4096):
    output = ctypes.create_string_buffer(size)
    count = wintypes.DWORD()
    buf = ctypes.create_string_buffer(source) if source is not None else None
    ok = reader.api.DeviceIoControl(reader.handle,
        (0xC657 << 16) | (command << 2), buf, len(source) if source is not None else 0,
        output, size, ctypes.byref(count), None)
    return bool(ok), output.raw, count.value


def run(dist):
    dist = Path(dist).resolve()
    work = Path(tempfile.mkdtemp(prefix="mam-test-", dir=ROOT / "build"))
    tape = work / "tape"
    tape.mkdir()
    mask = ctypes.windll.kernel32.GetLogicalDrives()
    letter = next(x for x in "YZVUSRQ" if not mask & (1 << (ord(x) - ord("A"))))
    mount = letter + ":"
    with (work / "format.log").open("wb") as log:
        subprocess.run([str(dist / "mkltfs.exe"), "-i", str(dist / "ltfs.conf"),
            "-e", "file", "-d", str(tape), "-s", "MAM001", "-n", "MAMTEST"],
            cwd=dist, stdout=log, stderr=log, check=True,
            creationflags=subprocess.CREATE_NO_WINDOW)

    def fixture(part, attribute, flags, value):
        (tape / f"attr_{part}_{attribute:x}").write_bytes(
            struct.pack(">HBH", attribute, flags, len(value)) + value)

    large = bytes(i % 256 for i in range(65535))
    fixture(0, 0xf100, 0x80, large)
    fixture(0, 0xf101, 0x01, b"")
    fixture(1, 0xf100, 0x01, b"partition one")
    # Force list pagination, including ID 0 and unknown IDs.
    fixture(0, 0, 0x80, b"\0" * 8)
    for attribute in range(0xd000, 0xd800):
        fixture(0, attribute, 0x80, b"\0\xff")

    with (work / "mount.log").open("wb") as log:
        process = subprocess.Popen([str(dist / "ltfs.exe"), mount, "-f",
            "-o", f"config_file={(dist / 'ltfs.conf').as_posix()}", "-o", "tape_backend=file",
            "-o", f"devname={tape.as_posix()}", "-o", "sync_type=close"], cwd=dist,
            stdout=log, stderr=log, creationflags=subprocess.CREATE_NO_WINDOW)
        reader = None
        try:
            for _ in range(100):
                if process.poll() is not None:
                    raise RuntimeError(f"Mount failed; see {work}")
                try:
                    reader = mam.MountedMAM(mount + "\\")
                    break
                except OSError:
                    time.sleep(0.1)
            assert reader is not None, f"Mount timed out; see {work}"
            ids = reader.read(0, 0)
            assert len(ids) > 4032 and b"\xf1\x00" in ids
            raw = reader.read(0, 1, 0xf100)
            assert raw == struct.pack(">HBH", 0xf100, 0x80, 65535) + large
            assert reader.read(0, 1, 0xf101) == bytes.fromhex("f101010000")
            assert reader.read(1, 1, 0xf100)[5:] == b"partition one"
            try:
                reader.read(0, 1, 0xffff)
                raise AssertionError("Missing attribute was accepted")
            except mam.GetterError:
                pass
            valid = struct.pack("<IBBHII", 1, 0, 0, 0, 0, 0).ljust(4096, b"\0")
            assert not control(reader, 0x83b, valid[:16])[0]
            assert not control(reader, 0x83b, valid, 2048)[0]
            assert not control(reader, 0x83b, None)[0]
            for offset, value in ((0, 2), (4, 2), (5, 2), (6, 1), (12, 1), (4095, 1)):
                bad = bytearray(valid)
                bad[offset] = value
                assert not control(reader, 0x83b, bytes(bad))[0]
            # Existing output-only commands retain protocol version 1.
            ok, old, count = control(reader, 0x801, None)
            assert ok and count == 4096 and struct.unpack_from("<IIi", old) == (0x474f544c, 1, 0)
            report = mam.export(reader)
            assert report["complete"]
            entries = {item["id"]: item for item in report["partitions"][0]["attributes"]}
            assert entries["0xF100"]["hex"] == large.hex()
            assert entries["0xF100"]["read_only"]
            assert entries["0xF101"]["length"] == 0
            # A malformed/truncated stored value must fail, not be zero-padded.
            fixture(0, 0xf102, 0, b"abc")
            (tape / "attr_0_f102").write_bytes(bytes.fromhex("f102000004") + b"abc")
            try:
                reader.read(0, 1, 0xf102)
                raise AssertionError("Truncated attribute was accepted")
            except mam.GetterError:
                pass
            print(f"MAM integration passed: {len(ids)//2} IDs, both partitions, "
                  f"65535-byte value, empty/missing/truncated values, request validation, "
                  f"legacy protocol and JSON export. Logs: {work}")
        finally:
            if reader:
                reader.close()
            # Only this disposable emulator process is stopped.
            process.terminate()
            process.wait(timeout=15)


if __name__ == "__main__":
    run(sys.argv[1])
