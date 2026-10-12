# Downshift architecture

## Implemented foundation

`src/main.cpp` creates `QApplication`, identifies the product as Downshift, initializes the process-wide libcurl runtime, validates dependencies, and opens `MainWindow`. Dependency failures return a nonzero exit code; normal startup presents a clear error dialog. The smoke-test path avoids dialogs and exits after checking window visibility and rendering.

`src/core/Runtime.*` owns libcurl initialization through a noncopyable RAII object. Local checks create and release a multi handle and an in-memory SQLite database with prepared statements. They exercise 64-bit values and validate required HTTP, HTTPS, TLS, and HTTP/2 capabilities. There are no transfers or persistent database operations in Phase 1.

`src/gui/MainWindow.*` contains the minimal Qt Widgets shell. Qt owns child widgets. Networking workers do not exist yet. Startup checks perform only bounded local operations.

`tests/FoundationTests.cpp` checks linked dependency functionality and repeated initialization/cleanup. CTest also launches the actual executable using minimal and Windows platform plugins. Both Debug and Release are intended to run in Windows CI. The minimal plugin is included in the selected Qt build; the offscreen plugin is not assumed to be available.

## Dependency and build decisions

- MSVC v143, Windows x64, C++20, shared MSVC runtime.
- Qt Core/Gui/Widgets through vcpkg `qtbase`, dynamically linked with `x64-windows`.
- libcurl with Windows Schannel TLS and HTTP/2. HTTP/3 is not currently enabled; optional support will be detected at runtime when a suitable build is chosen.
- SQLite through its C API, avoiding a second Qt SQL persistence abstraction.
- One vcpkg commit pins the tool checkout and the complete baseline dependency set. No arbitrary dependency upgrades during configure.
- CMake's Visual Studio 2022 generator locates MSVC without requiring a developer shell. CMake 3.25 is the minimum supported version, not a claim of bit-for-bit compiler reproducibility.
- Qt Windows and minimal plugins are copied beside the development executable. Installer/portable packaging belongs to Phase 9.

## Planned module boundaries

These modules are reserved as directories; their functionality is not implemented:

| Module | Approved future responsibility |
|---|---|
| core/DownloadEngine | libcurl multi event loop, safe HTTP response handling |
| core/SegmentScheduler | nonoverlapping dynamic byte-range assignments |
| core/ConnectionManager | per-host limits, reuse, bandwidth control |
| core/FileWriter | 64-bit positional writes, partial files, safe finalization |
| core/ResumeManager | validators, verified ranges, crash recovery |
| core/DownloadQueue | simultaneous download policy, priorities |
| database | SQLite queue/history/settings, worker ownership |
| browser | Native Messaging host, secure per-user IPC |
| utils | shared validation and secret-safe diagnostics as needed |
| extension | Manifest V3 browser handoff state machine |

The future engine must keep network and disk work off the GUI thread and publish bounded progress updates through queued Qt communication. Persistent SQLite access will use explicit ownership and transactions; thread-safe SQLite compilation alone is not a concurrency design.

## Planned security boundaries

TLS verification remains enabled. Future transfers accept only validated HTTP/HTTPS URLs, sanitize destination filenames, prevent traversal, and validate every range response. Browser communication must restrict extension origins and authenticate per-user IPC. Credentials and signed URLs must not appear in logs or plaintext persistence. No downloaded file runs automatically.

These security behaviors are requirements for future phases, not claims that an unimplemented engine or extension already enforces them.
