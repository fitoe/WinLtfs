# Native Win32 mount manager (draft)

Small C++17/Win32 GUI: tape device, available drive letter, one state-dependent Mount / Safely unmount button, and session logs. Device/letter lists refresh automatically every three seconds while idle (not while their dropdowns are open). The last successfully mounted letter is saved, with an available fallback if occupied. Advanced options stay collapsed and offer Restore defaults. No .NET, WinForms, service, or installer. MinGW C++/thread runtimes are statically linked; the executable imports Windows system DLLs only (including Windows 10/11's UCRT). WinLtfs's own DLL distribution and an installed WinFsp driver are still required.

Build on Windows with MSYS2 UCRT64 GCC:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File gui/win32/build.ps1 -Compiler C:/msys64/ucrt64/bin/g++.exe
```

Run `gui/win32/bin/WinLtfsManager.exe`. Engine discovery uses a saved location, a sibling `engine` directory, the executable directory, then an ancestor's `dist` directory. A locate button appears only when required files are missing. Settings and session logs are under `%LOCALAPPDATA%/WinLtfsManager/`. The engine is not bundled by this build.

## Lifecycle and scope

- Device listing uses QueryDosDevice only; startup does not open any tape.
- Rechecks device presence and drive-letter availability before starting. Another `ltfs.exe` process blocks a new mount. A per-session named mutex prevents duplicate native-manager instances.
- Creates the engine in a hidden, separate console with stdout/stderr redirected to a session log. A short-lived helper attaches to that console to send Ctrl+C for normal unmount.
- Mount/unmount waits run off the UI thread. Unmount requires successful process exit and the `Volume unmounted successfully` log message. Timeouts retain the process; there is no forced kill. Window closing requests normal unmount first. Forced termination of the GUI is not clean unmount.
- Mount can update tape metadata. No software read-only checkbox: a v1.0.0 emulator trial allowed writes despite `-o ro`. Use cartridge hardware write protection when read-only access is required.
- No format, repair, eject, automatic mount, driver installation, or external-session management.
- English log-message detection, fixed layout, packaging, mount-time cancellation and broader error/write-durability coverage remain draft limitations. Normal UI shutdown is tested; abrupt OS/process termination is not made safe by this wrapper.

## Advanced options

The expandable panel exposes logging (normal/warnings/debug), index synchronization (periodic/file-close/unmount), periodic interval (1–1440 minutes), and min/max write-cache sizes (1–4096 MiB, minimum <= maximum). Defaults match the engine: normal logging, five-minute sync, 25/50 MiB cache. Options are constructed as individual validated -o arguments; no arbitrary command line is accepted. Write-cache tuning is not a read-speed fix. Advanced values are session-only and cannot change a running mount. The emulator smoke test passes the default advanced options through the engine parser.

## Offline smoke test

Use an existing file-emulator directory and an unused letter, with no other LTFS process running:

```powershell
./gui/win32/bin/WinLtfsManager.exe --smoke C:/path/to/dist C:/path/to/faketape W:
```

Explicitly selects the file backend; never formats media. Exercises startup, readiness detection, Ctrl+C shutdown, exit status and clean-unmount log confirmation. Exit code zero indicates success. The native version passed this test on Windows 11; the test drive letter disappeared afterward. No physical tape was opened for GUI development. Import-table inspection found only Windows system DLLs, with no .NET or MinGW runtime DLL dependency.

Separate engine-only tests with WinLtfs v1.0.0 on HPE LTO-6 copied a 1,780,275,289-byte ZIP in 17.07 s using CopyFileEx (middle intervals 158–162 MiB/s) and 18.75 s using Windows Shell. Both full hashes matched. Those are not GUI hardware/write-safety validation or cold-cache benchmark claims.
