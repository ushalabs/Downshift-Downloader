# Downshift setup

## End-user installation

No installer, portable package, or verified release binary is available yet. Standalone distribution is scheduled for Phase 9. End users will not require build tools once a verified distribution exists.

## Development prerequisites

- Windows 10/11 x64.
- Visual Studio 2022 or Build Tools 2022, MSVC v143, Desktop development with C++, and a Windows SDK.
- Git.
- CMake 3.25 or newer, either on PATH or bundled with Visual Studio's C++ CMake tools.
- Python 3.10+ for controlled HTTP/HTTPS integration tests only. The application itself does not need Python.
- Internet access and sufficient disk space for a Qt source build and its dependencies.

Use Microsoft's official [Build Tools](https://visualstudio.microsoft.com/downloads/#build-tools-for-visual-studio-2022) and the official [CMake downloads](https://cmake.org/download/). Installing tools may require administrator permission. The project bootstrap does not install system software.

You can run `./scripts/Install-BuildTools.ps1` from your own PowerShell window to download Microsoft's official installer, verify its Microsoft signature, and request Windows administrator approval for the C++ workload and recommended SDK components. Install CMake or select Visual Studio's C++ CMake tools component, and make CMake available on PATH.

## Configure and build

```powershell
git clone https://github.com/ushalabs/Downshift-Download-Manager.git
cd Downshift-Download-Manager
./scripts/Bootstrap.ps1
./scripts/Test-Environment.ps1
cmake --preset windows-msvc
cmake --build --preset release --parallel 2
ctest --preset release
cmake --build --preset debug --parallel 2
ctest --preset debug
```

Bootstrap clones vcpkg into `.tools/windows/vcpkg`, checks out the manifest's exact baseline, disables vcpkg metrics, and sets `VCPKG_ROOT` for the current session. It also finds Visual Studio's bundled CMake when CMake is absent from PATH. It refuses to change an existing checkout at a different revision. The configure step installs dependencies through manifest mode.

If Python is not on PATH, add `-DPython3_EXECUTABLE=C:/path/to/python.exe` to the configure command. Set `-DBUILD_TESTING=OFF` only for an application-only build; that excludes the acceptance tests.

In a new PowerShell session, set the checkout location before configuring:

```powershell
./scripts/Bootstrap.ps1
```

For an external tools location, pass `-ToolsDirectory 'C:\your\tools'` to bootstrap. Keep vcpkg checkout history available for manifest version resolution.

## Launch and verify

```powershell
./build/windows-msvc/Release/Downshift.exe
```

The window shows Downshift and the linked dependency versions. Full desktop download controls are Phase 6. CTest checks its rendering/event loop with `qwindows` and `qminimal`. Phase 2 provides a native development download client:

```powershell
./build/windows-msvc/Release/DownshiftDownload.exe --help
./build/windows-msvc/Release/DownshiftDownload.exe 'https://example.com/file.zip' "$env:USERPROFILE\Downloads\file.zip"
```

Arguments: URL, explicit destination, optional `--sha256 HEX`, `--ca-bundle FILE`, and `--timeout-ms N`. Automatic tuning is the default. Use `--connections 1`, `4`, `8`, or `16` for a fixed limit (other values from 1-16 are accepted), or `--connections auto`. `--chunk-kib N` fixes chunk size instead of adapting. `--progress` shows live speed/ETA; `--json` produces final measurements and `--progress-json` emits structured snapshots on stderr. Rates use bytes/second in JSON and MiB/s in human output.

The destination parent must exist; existing destinations/partials are never overwritten. The trust bundle preserves peer/hostname verification. Do not use the public test key/certificate for real downloads. Failures leave `.part`; choose a new name or review/remove the partial yourself before a fresh attempt. Resume is Phase 4.

### Verified local build (2026-10-12)

Windows x64 builds passed with MSVC 19.44.35229.0 (v143 toolset 14.44.35207), Windows SDK 10.0.26100.0, and Visual Studio's CMake 3.31.6-msvc6. Phase 1 passed all three foundation checks in Release and Debug; Phase 2 adds the native engine and integration groups. Current results and the separate CI status are in [development progress](DEVELOPMENT_PROGRESS.md). Qt 6.11.2, libcurl 8.22.0, and SQLite 3.53.4 use the unchanged pinned manifest.

On the active development machine, the Release executable is `C:\Users\HP\Desktop\Projects\Downshift\build\windows-msvc\Release\Downshift.exe`. Keep it beside the copied DLLs and `platforms` folder. This is a development build; installer and portable distribution are later-phase work.

## Pinned dependency set

Registry/tool revision: `e456309491fd875487bf02b8a0dca76c1b1b00cc`.

| Direct dependency | Registry version | Selected features |
|---|---|---|
| qtbase | 6.11.2 | widgets, thread and their required dependencies |
| curl | 8.22.0, port revision 1 | ssl, http2 |
| sqlite3 | 3.53.4, port revision 2 | no optional features |

The same immutable baseline governs transitive dependencies. Manifest versions follow [vcpkg's versioning rules](https://learn.microsoft.com/en-us/vcpkg/users/versioning). Updating the baseline is a deliberate source change followed by validation.

## Troubleshooting

- Missing compiler: install the C++ workload and SDK, then rerun the environment check.
- Missing CMake: add its `bin` directory to PATH and reopen PowerShell.
- Missing `VCPKG_ROOT`: run bootstrap in the same session or set it explicitly.
- Existing vcpkg revision mismatch: choose a separate tools directory rather than resetting another project's checkout.
- Dependency download failures: check network access and trusted certificates. Do not disable TLS verification.
- Missing Qt plugin/DLL: rebuild the executable in the prescribed configuration; run beside its copied development dependencies.

## Chrome integration

The extension and Native Messaging host are scheduled for Phase 7. No browser permissions or registry changes are performed during Phase 1.
