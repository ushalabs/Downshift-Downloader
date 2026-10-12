# Downshift development progress

## Phase gate

- Current phase: 2 - Segmented Download Engine.
- Last approved phase: 2, explicitly approved by the user's "Go ahead" on 2026-10-12.
- Last completed phase: 2 - Segmented Download Engine (2026-10-12).
- Status: Phase 2 completed and verified locally; awaiting Phase 3 approval.
- Product name: Downshift.
- Active local project: `C:\Users\HP\Desktop\Projects\Downshift`.
- Former workspace copy remains as a backup; removing it was blocked by the execution approval policy. Do not develop in that copy.
- Git workflow: the user commits and pushes; the assistant must not commit or push automatically.
- Authorized repository: https://github.com/ushalabs/Downshift-Download-Manager.
- Phase 2 authorization: granted. Phase 3 authorization: not granted.

## Completed work

- Inspected the empty workspace and confirmed the authorized GitHub repository is empty.
- Cloned that repository without replacing existing history.
- Established C++20/Qt Widgets skeleton and modular directories.
- Integrated libcurl and SQLite in CMake with local runtime checks and RAII cleanup.
- Added x64 MSVC Debug/Release presets and a fixed vcpkg baseline.
- Added dependency lifecycle/64-bit SQLite checks and executable startup/rendering smoke tests.
- Prepared a Windows CI workflow and development bootstrap/environment scripts.
- Documented build steps, planned architecture, phase boundaries, and licensing obligations.
- Transferred the project from the chat workspace to the requested Desktop directory on 2026-10-12; all 96 transferred files, including Git history, were hash-verified.
- Updated repository instructions to follow the user's manual commit/push workflow, overriding the original attached prompt.
- Verified the user's C++ Build Tools installation, prepared pinned dependencies, and built the application in Release and Debug from the Desktop project.
- Ran all three foundation checks in each configuration: six passed, zero failed.

## Environment and verification

Initial local inspection on the client date 2026-10-11:

- Windows x64, reported version 10.0.26200.0.
- Git 2.55.0.windows.5 available.
- MSVC, CMake, Qt, and vcpkg not found in the inspected locations or PATH.
- GitHub connector authentication verified as ushalabs; authorized repository grants push access.
- Git transport works with the OpenSSL backend; the default transport stalled.
- Initial local compilation and CTest: not executed because the toolchain was absent.
- The user approved installing official Microsoft C++ Build Tools on 2026-10-12. At that earlier checkpoint, the additional system-directory permission request returned no grants and the assistant process was not an administrator, so the assistant did not perform the system installation. The user subsequently installed the tools successfully.
- Prepared `scripts/Install-BuildTools.ps1` for the user to run directly with Windows administrator approval; it downloads the official installer and verifies its Microsoft signature before launching it.
- The user installed Visual Studio Build Tools 2022, verified on 2026-10-12: Visual Studio 17.14.37710.0, MSVC toolset 14.44.35207, Windows SDK 10.0.26100.0, bundled CMake 3.31.6-msvc6.
- Prepared and bootstrapped the pinned vcpkg checkout in `.tools/windows/vcpkg`. The old incomplete `.tools/vcpkg` checkout is preserved because it has a stale lock. Bootstrap now defaults to the usable tools folder and locates Visual Studio's bundled CMake automatically.
- Local prerequisite checks passed. All thirteen dependency packages built and installed successfully in Debug and Release. Application compilation and all foundation tests passed in both configurations.
- The automatically selected Qt mirror was slow. Downloaded the identical source archive from a mirror advertised by Qt (`ftp.fau.de`), verified its SHA-512 against the pinned port, cached it locally, and resumed configure. No dependency revision or TLS verification settings were changed.
- Earlier Windows CI run 38165976115: failed during CMake generation because the selected Qt build does not contain `Qt6::QOffscreenIntegrationPlugin`. All 13 dependency packages built successfully.
- Build environment from that run: MSVC 19.44.35229.0, toolset 14.44.35207, Windows SDK 10.0.26100.0, Windows Server 2022 runner.
- Fixed locally on 2026-10-12: use the available `Qt6::QMinimalIntegrationPlugin` for the headless startup check, while retaining the Windows plugin test. Both checks passed locally in Release and Debug. The corrected CI workflow has not been pushed or rerun.
- Updated local CI workflow to preserve completed dependency builds even when the application build fails.
- Local PowerShell parser, JSON manifest/presets, and whitespace checks: passed.
- Benchmarks and IDM comparison: not executed; outside Phase 1.

### Verified local acceptance results (2026-10-12)

Environment: Windows 10.0.26200 x64, Intel Core i7-1185G7, MSVC 19.44.35229.0, toolset 14.44.35207, MSBuild 17.14.60, Windows SDK 10.0.26100.0, CMake 3.31.6-msvc6. vcpkg uses its own acquired CMake 4.4.3 for dependency compilation.

| Check | Release | Debug |
|---|---|---|
| Application and foundation-test compilation | Passed | Passed |
| `foundation.dependencies` | Passed (0.11 s) | Passed (0.14 s) |
| `foundation.gui.minimal` | Passed (0.55 s) | Passed (0.64 s) |
| `foundation.gui.windows` | Passed (0.71 s) | Passed (0.54 s) |
| CTest summary | 3 passed, 0 failed; 1.42 s total | 3 passed, 0 failed; 1.35 s total |

The dependency test repeats initialization/cleanup three times, creates libcurl multi handles, confirms HTTP/HTTPS/TLS/HTTP2 capability, and exercises SQLite 64-bit writes/reads. Both GUI checks launch the actual Downshift executable, run its event loop, verify window identity/visibility and a rendered widget image, then exit. These durations are test execution times, not startup-performance or download benchmarks.

Release executable: `build/windows-msvc/Release/Downshift.exe`. Debug executable: `build/windows-msvc/Debug/Downshift.exe`. Copied DLLs and Qt platform plugins reside beside each executable. Verbose build/test logs are preserved locally as `build/phase1-{release,debug}-{build,tests}.log`; generated files and dependencies are ignored by Git.

Acceptance: the pinned Windows build produces a functioning native Qt skeleton in both configurations. No Phase 1 acceptance blocker remains. Latest source changes remain uncommitted and unpushed for the user; updated CI verification remains pending manual publication.

## Decisions

Keep the required native Windows stack. Qt uses dynamic linkage, libcurl uses Schannel and HTTP/2, and SQLite uses its C API. HTTP/3 is optional and not selected. Phase 2 implements the download engine, fixed scheduler, and file writer. Other download modules and Chrome integration remain reserved for later phases.

Expose the Phase 2 engine through a native console development client, keeping blocking network/disk work outside the Qt GUI thread. Full desktop controls remain Phase 6. Segmentation requires a strong ETag and valid actual range probe; otherwise use a complete stream. Fixed four-connection/4 MiB policy is the correctness baseline for Phase 3 tuning. Failed partials remain untouched by later attempts until Phase 4 supplies verified recovery.

The vcpkg baseline and tool checkout are pinned to `e456309491fd875487bf02b8a0dca76c1b1b00cc`. Minimum CMake is 3.25; the compiler/SDK versions from the verified build are recorded above.

## Phase handoff

Phase 2 implementation, builds, tests, fixes, and documentation are complete locally. The user reviews, commits, and pushes these working-tree changes. The assistant stops here until the user explicitly approves Phase 3: adaptive connection/chunk tuning, speed calculations, and initial performance benchmarks. No Phase 3 implementation or performance claim is included.

## Phase 2 acceptance (2026-10-12)

Approved by the user's "Go ahead" after the Phase 1 handoff. Implemented libcurl multi HTTP/HTTPS transfers, actual range probing, strict response validation, fixed concurrent segments, strong ETag/If-Match checks, streaming fallback, 64-bit partial-file writes, CNG SHA-256, safe finalization, a native development client, and controlled tests. TLS peer/hostname/revocation checks remain enabled. HTTPS CONNECT proxy headers are excluded from file-response validation and covered by a local restricted-proxy test.

Same verified Windows/MSVC/SDK/dependency environment as Phase 1; Python 3.12.14 runs only the local test servers. No dependency baseline changes or new system installations. Both configurations compile without compiler warnings/errors.

| Check | Release | Debug |
|---|---|---|
| GUI, client, engine, and test compilation | Passed | Passed |
| `engine.unit` (4,162 assertions, including an actual sparse file write/read beyond 4 GB) | Passed (0.14 s) | Passed (0.16 s) |
| `engine.integration` (52 loopback HTTP/HTTPS scenarios) | Passed (5.83 s) | Passed (6.21 s) |
| `foundation.dependencies` | Passed (0.05 s) | Passed (0.04 s) |
| `foundation.gui.minimal` | Passed (0.34 s) | Passed (0.37 s) |
| `foundation.gui.windows` | Passed (0.49 s) | Passed (0.60 s) |
| CTest summary | 5 passed, 0 failed; 6.89 s total | 5 passed, 0 failed; 7.43 s total |

Successful fixture downloads are checked byte-for-byte and against independent Python SHA-256 digests. The 17 MiB payload needs five chunks, verifying concurrency limits and reassignment after the initial four. Tests check complete nonoverlapping coverage, safe fallback after data transmission, malformed/changed/truncated responses, certificate/hostname failures, redirect policy, existing-file preservation, and cooperative cancellation. Detailed scope and unverified areas are in `docs/TESTING.md`.

During verification, fixed Windows macro interference in compilation and a CONNECT-header classification issue. The initial CA-signed test certificate lacked a revocation service; replaced the fixture with a self-signed, explicitly trusted test root without changing any client verification setting. Initial test failures were resolved before the successful runs above.

Development client: `build/windows-msvc/Release/DownshiftDownload.exe` (and Debug equivalent). Usage and safe partial-file handling are in `docs/INSTALLATION.md`. The desktop GUI is still the foundation shell. Build and verbose test logs: `build/phase2-{release,debug}-{build,tests}.log`. Generated binaries, logs, dependency caches, and Python caches are ignored.

Acceptance: correct HTTP/HTTPS files from range and non-range servers, verified by checksums, with safe partials/finalization and passing Debug/Release checks. No local Phase 2 acceptance blocker remains. HTTP/2 capability is enabled but the fixture tests HTTP/1.1 wire behavior; throughput/IDM comparisons, retry/resume, persistent queues, and full GUI are future phases. Phase 2 CI has not been run or claimed successful; publication remains the user's action.

## Git checkpoint

- Latest user-created commit: `8dddb3c47d0c21ed729ac8c165eff1d197612dc0` (`Phase 1`); working tree was clean at Phase 2 start.
- Published WIP checkpoint: `935f1e33e7033c866872a375abf5b8c5c0afbd06`.
- Remote push: verified on `dev/phase-01-foundation` by matching the remote ref.
- Earlier inspected CI result: failed; https://github.com/ushalabs/Downshift-Download-Manager/actions/runs/38165976115. The user subsequently committed Phase 1 locally. No Phase 2 CI run has been executed by the assistant.
- The WIP commit and push occurred before the user changed the workflow. No further automatic commits or pushes are authorized.
- Phase 2 changes: left uncommitted and unpushed for the user. No assistant Git commits/pushes were made during Phase 2.
- Next action: the user reviews/publishes Phase 2 and explicitly approves Phase 3 when ready. The assistant must not commit/push automatically or start Phase 3 before approval. Current local tests passed; no successful CI run is claimed.

Use Git history to resolve the current document's containing commit; a commit cannot embed its own hash. Record earlier verified commit IDs here once available.
