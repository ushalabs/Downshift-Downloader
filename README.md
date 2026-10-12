# Downshift

Downshift is a free, open-source native Windows download manager being developed in ten phases. The product name is **Downshift**; the executable is `Downshift.exe`.

**Phases 1-8 are implemented and verified locally.** The adaptive native engine supports safe pause/resume, bounded retries, verified crash recovery, and streaming fallback. The encrypted persistent queue provides priorities, simultaneous downloads, origin limits, and bandwidth pacing. Native desktop controls and a Chrome extension connect to those workers. See [development progress](docs/DEVELOPMENT_PROGRESS.md) for acceptance results and [Chrome setup](docs/CHROME.md) for local browser installation. No direct IDM performance comparison has been performed.

## Implemented features

- Native Qt 6 desktop with working download controls, live details, history/search, and light/dark themes.
- libcurl process lifetime management and multi-handle capability check.
- SQLite integration with an in-memory 64-bit write/read check.
- Windows x64 MSVC CMake presets and pinned vcpkg dependency baseline.
- Dependency and Qt startup smoke tests, using the Windows and minimal platform plugins.
- Windows build workflow for Debug and Release; local verification and separate CI status are recorded in the development progress document.
- libcurl multi transfers with automatic tuning by default, fixed 1-16 connection limits, and adaptive 256 KiB-16 MiB chunks.
- Actual `bytes=0-0` capability probe, exact range validation, and strong ETag/`If-Match` version checks.
- Streaming for servers ignoring ranges or lacking a strong validator; safe restart if range support disappears.
- 64-bit file writes, exclusive `.part` files, optional SHA-256 verification, and finalization without overwriting an existing file.
- Controlled loopback HTTP/HTTPS integration tests and native file/protocol tests.
- Live speed/ETA, structured measurements, process CPU/memory and file-call timings.
- Console pause/resume, encrypted range checkpoints, local/remote revalidation, and recovery after forced termination.
- Bounded transient retries with jitter/Retry-After and explicit archival restart when safe resume is unavailable.
- Persistent SQLite queue/history/settings with user-scoped encryption, priorities, concurrency limits, and global/per-download pacing.
- Manifest V3 Chrome extension, restricted Native Messaging/Windows IPC, durable handoff receipts, and tested browser fallback for unsupported or unavailable transfers.
- Bounded segmented write buffers, larger checksum reads, recovery-metadata contention handling, and structured I/O/checkpoint counters.
- Reproducible paired loopback benchmarks: 180 independently verified 100 MB/1 GB/5 GB samples across fixed counts, automatic mode, streaming, and traffic caps. See [performance results](docs/PERFORMANCE.md) for measured gains, regressions, and limits.

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
4. Resume and recovery (completed locally).
5. Queue and persistence (completed locally).
6. Modern desktop GUI (completed locally).
7. Chrome integration (completed locally).
8. Performance optimization.
9. Windows packaging.
10. GitHub release.

The user approved Phase 8 after the completed Phase 5-7 batch. All Phase 8 Git operations belong to the user; [the Git handoff](docs/GIT_HANDOFF.md) explains committing the verified changes, creating `main`, and merging the existing development branch. Phase 9 packaging requires further approval. See [development progress](docs/DEVELOPMENT_PROGRESS.md) and [project requirements](docs/PROJECT_REQUIREMENTS.md).

Active local project directory: `C:\Users\HP\Desktop\Projects\Downshift`.

## Documentation

- [Architecture](docs/ARCHITECTURE.md)
- [Development progress](docs/DEVELOPMENT_PROGRESS.md)
- [Setup and installation](docs/INSTALLATION.md)
- [Benchmark status and methodology](docs/BENCHMARKS.md)
- [Phase 8 optimization and paired measurements](docs/PERFORMANCE.md)
- [IDM comparison availability](docs/IDM_COMPARISON.md)
- [Manual Git steps: create main and merge](docs/GIT_HANDOFF.md)
- [Third-party licensing](docs/THIRD_PARTY_NOTICES.md)
- [Engine tests](docs/TESTING.md)
- [Pause/resume and recovery](docs/RECOVERY.md)
- [Persistent queue](docs/QUEUE.md)
- [Desktop usage](docs/DESKTOP.md)
- [Chrome setup and handoff recovery](docs/CHROME.md)

## Download a file during development

From the project folder, replace the URL and destination with your own values:

```powershell
./build/windows-msvc/Release/DownshiftDownload.exe 'https://example.com/file.zip' "$env:USERPROFILE\Downloads\file.zip" --progress
```

Use `--sha256` followed by a known digest for independent integrity verification. `--help` lists the options. The destination directory must exist. A fresh request requires absent destination/partial/recovery files. Press Ctrl+C to pause; use the identical URL/destination with `--resume` to reuse verified ranges. `--restart` archives old partials before downloading again. See [recovery](docs/RECOVERY.md) for validator requirements and unsupported cases. Completed destinations are never overwritten.

Automatic mode adjusts future assignments after throughput trials; existing ranges drain safely. Use `--connections 4 --chunk-kib 4096` for the previous fixed baseline or `--connections 1`, `8`, or `16` for other limits. `--json` returns measured results. Benchmark reproduction and limitations are in [benchmarks](docs/BENCHMARKS.md).

## Limitations

Installers and release binaries belong to later phases. Chrome handoff supports public range-capable files with strong validators; browser login/cookies are not forwarded, and personalized resources should stay in Chrome. Its popup toggle defaults off and needs explicit local setup. Partial resume requires actual range support and an unchanged strong ETag; streaming and unverifiable partials need an explicit restart. Recovery records normally require the same Windows user/profile. Automatic tuning cannot guarantee the best throughput on every server. HTTP/3 is not enabled. An unframed response ending at EOF cannot independently prove its intended length; use a trusted SHA-256 when available. No download is executed automatically.

## License

Downshift source code uses the [MIT License](LICENSE). Dependencies retain their own licenses. Qt is dynamically linked; distribution must fulfill the applicable LGPL obligations and include all required dependency notices. See [third-party licensing](docs/THIRD_PARTY_NOTICES.md).
