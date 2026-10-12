# Downshift

Downshift is a free, open-source native Windows download manager being developed in ten approved phases. The product name is **Downshift**; the executable is `Downshift.exe`.

**Current status: Phase 3 completed locally.** Downshift downloads HTTP/HTTPS files through the development console client, with adaptive ranges/concurrency, live speed/ETA, verified content, and streaming fallback. Debug and Release each passed six test groups, including 52 protocol scenarios and 12 adaptive scenarios. Repeated local benchmarks accepted 108 checksum-verified samples, including 5 GB files. The desktop window remains the foundation shell; the full interface is Phase 6. See [development progress](docs/DEVELOPMENT_PROGRESS.md). No direct IDM comparison has been performed.

## Implemented features

- C++20 and Qt 6 Widgets application shell.
- libcurl process lifetime management and multi-handle capability check.
- SQLite integration with an in-memory 64-bit write/read check.
- Windows x64 MSVC CMake presets and pinned vcpkg dependency baseline.
- Dependency and Qt startup smoke tests, using the Windows and minimal platform plugins.
- Windows build workflow for Debug and Release; the corrected workflow awaits the user's commit/push and a new CI run. Local verification is recorded in the development progress document.
- libcurl multi transfers with automatic tuning by default, fixed 1-16 connection limits, and adaptive 256 KiB-16 MiB chunks.
- Actual `bytes=0-0` capability probe, exact range validation, and strong ETag/`If-Match` version checks.
- Streaming for servers ignoring ranges or lacking a strong validator; safe restart if range support disappears.
- 64-bit file writes, exclusive `.part` files, optional SHA-256 verification, and finalization without overwriting an existing file.
- Controlled loopback HTTP/HTTPS integration tests and native file/protocol tests.
- Live speed/ETA, structured measurements, process CPU/memory and file-call timings.
- Reproducible loopback benchmarks for 100 MB, 1 GB, and 5 GB files; SHA-256 verification before accepting samples.

## Build

Install Visual Studio 2022 Build Tools with the **Desktop development with C++** workload, MSVC v143, and a Windows 10/11 SDK. Install Git, CMake 3.25 or newer, and Python 3.10+ for the test servers. Python is not required by the native application. See [installation and development setup](docs/INSTALLATION.md).

From PowerShell in the repository root:

```powershell
./scripts/Bootstrap.ps1
./scripts/Test-Environment.ps1
cmake --preset windows-msvc
cmake --build --preset release --parallel 2
ctest --preset release
./build/windows-msvc/Release/Downshift.exe
```

The first configure builds the dependency set from source and can take substantial time and disk space. Run bootstrap and configure in the same PowerShell session so `VCPKG_ROOT` remains available. Bootstrap also finds Visual Studio's bundled CMake when it is absent from PATH.

## Roadmap and phase gate

1. Native C++ foundation (completed locally).
2. Segmented download engine (completed locally).
3. Adaptive performance engine (completed locally).
4. Resume and recovery.
5. Queue and persistence.
6. Modern desktop GUI.
7. Chrome integration.
8. Performance optimization.
9. Windows packaging.
10. GitHub release.

Each phase is implemented, built, tested, and documented locally, then reported for explicit approval of the next phase. The user handles commits and GitHub pushes; the assistant does not publish automatically. This direct user instruction overrides the automatic Git workflow in the original attached prompt. See [development progress](docs/DEVELOPMENT_PROGRESS.md) and [project requirements](docs/PROJECT_REQUIREMENTS.md).

Active local project directory: `C:\Users\HP\Desktop\Projects\Downshift`.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Development progress](docs/DEVELOPMENT_PROGRESS.md)
- [Setup and installation](docs/INSTALLATION.md)
- [Benchmark status and methodology](docs/BENCHMARKS.md)
- [Third-party licensing](docs/THIRD_PARTY_NOTICES.md)
- [Engine tests](docs/TESTING.md)

## Download a file during development

From the project folder, replace the URL and destination with your own values:

```powershell
./build/windows-msvc/Release/DownshiftDownload.exe 'https://example.com/file.zip' "$env:USERPROFILE\Downloads\file.zip" --progress
```

Use `--sha256` followed by a known digest for independent integrity verification. `--help` lists the options. The destination directory must exist, and both the destination and its `.part` sibling must be absent. The engine checks response headers and byte counts, then flushes and renames the partial file. Failed transfers leave `.part` files for inspection; resume belongs to Phase 4.

Automatic mode adjusts future assignments after throughput trials; existing ranges drain safely. Use `--connections 4 --chunk-kib 4096` for the previous fixed baseline or `--connections 1`, `8`, or `16` for other limits. `--json` returns measured results. Benchmark reproduction and limitations are in [benchmarks](docs/BENCHMARKS.md).

## Limitations

Pause/resume, retries, recovery, a persistent queue, desktop download controls, Chrome interception, themes, installers, and release binaries belong to later phases. Automatic tuning is a conservative heuristic and cannot guarantee the best throughput on every server. HTTP/3 is optional and is not enabled. An unframed response ending at EOF cannot independently prove its intended length; use a trusted SHA-256 when available. No download is executed automatically.

## License

Downshift source code uses the [MIT License](LICENSE). Dependencies retain their own licenses. Qt is dynamically linked; distribution must fulfill the applicable LGPL obligations and include all required dependency notices. See [third-party licensing](docs/THIRD_PARTY_NOTICES.md).
