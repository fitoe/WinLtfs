# Experimental Windows mount manager

A small, standalone .NET 8 WinForms frontend for WinLtfs. This is a draft for discussion, not a replacement for the CLI or a production write-safety claim.

## Build and run

Install the .NET 8 SDK, then run from the repository root:

```powershell
dotnet build gui/WinLtfsManager -c Release
./gui/WinLtfsManager/bin/Release/net8.0-windows/WinLtfsManager.exe
```

Runtime requirements: Windows x64, .NET 8 Desktop Runtime, an installed WinFsp driver, and the WinLtfs executable/DLL distribution. The GUI searches its saved directory, an `engine` directory beside the GUI, the GUI directory itself, and an ancestor's `dist` directory. If not found, it offers a locate button; normal operation does not expose an engine-path field. The engine is not bundled by this project.

Select a tape device and unused drive letter, then Mount. Unmount requests Ctrl+C through a separate helper process attached to the engine's console, waits for process exit, and checks the clean-unmount message and exit code. It does **not** kill the engine on timeout. Window closing requests normal unmount first. A failed or timed-out operation remains visible. Logs and the generated plugin configuration are under `%LOCALAPPDATA%/WinLtfsManager/sessions/`.

The startup device list only queries the DOS device namespace; no tape is opened until Mount. An existing `ltfs` process blocks another mount. The UI does not manage mounts started elsewhere. There is no formatting, repair, eject, service installation, or automatic mounting.

## Limitations and validation

- Mounting can update tape metadata. Use cartridge hardware write protection for read-only access. A local v1.0.0 file-emulator test allowed writes despite `-o ro`, so this UI intentionally makes no software read-only promise.
- Normal-unmount detection currently depends on the engine's English log message and exit status. Engine/version changes may require revisiting this contract.
- The engine runs as a separate process. Forced GUI termination is not a clean-unmount mechanism; users must not power off the drive simply because the window disappeared.
- Mount waits up to three minutes before returning control; unmount waits up to 90 seconds. Timeouts preserve the process rather than implying success.
- Localization, release packaging, cancellation during the mount wait, and more extensive failure/physical-write tests remain follow-up work.

Internal smoke test (requires an **existing file-emulator tape directory**, never formats media):

```powershell
./gui/WinLtfsManager/bin/Release/net8.0-windows/WinLtfsManager.exe --smoke C:/path/to/dist C:/path/to/faketape W:
```

This path explicitly selects the file backend, mounts it, checks readiness, requests normal unmount, and returns a nonzero exit code on failure. Run it only with an unused letter and no other `ltfs` process. The draft was built and smoke-tested this way on Windows 11; no physical tape was accessed by the GUI tests.

Separately, the underlying WinLtfs v1.0.0 release was tested on an HPE LTO-6 with a 1,780,275,289-byte ZIP: CopyFileEx completed in 17.07 s (middle intervals around 158–162 MiB/s), and Windows Shell copying completed in 18.75 s. Both destination SHA256 hashes matched. Those are engine read-path observations, not GUI-driven write/durability validation or a controlled cold-cache benchmark.
