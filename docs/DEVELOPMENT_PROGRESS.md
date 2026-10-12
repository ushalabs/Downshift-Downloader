# Downshift development progress

## Phase gate

- Current phase: 1 - Native C++ Foundation.
- Last approved phase: 1, confirmed by the user's direct request to continue development.
- Last completed phase: 1 - Native C++ Foundation (2026-10-12).
- Status: completed and verified locally; awaiting explicit Phase 2 approval.
- Product name: Downshift.
- Active local project: `C:\Users\HP\Desktop\Projects\Downshift`.
- Former workspace copy remains as a backup; removing it was blocked by the execution approval policy. Do not develop in that copy.
- Git workflow: the user commits and pushes; the assistant must not commit or push automatically.
- Authorized repository: https://github.com/ushalabs/Downshift-Download-Manager.
- Phase 2 authorization: not granted; do not begin Phase 2.

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

Keep the required native Windows stack. Qt uses dynamic linkage, libcurl uses Schannel and HTTP/2, and SQLite uses its C API. HTTP/3 is optional and not selected currently. Download modules are directories reserved for later phases; no transfer engine or Chrome integration is implemented.

The vcpkg baseline and tool checkout are pinned to `e456309491fd875487bf02b8a0dca76c1b1b00cc`. Minimum CMake is 3.25; the compiler/SDK versions from the verified build are recorded above.

## Phase handoff

Phase 1 implementation, compilation, tests, fixes, and documentation are complete locally. The user reviews, commits, and pushes the changes. The assistant stops until the user explicitly approves Phase 2: HTTP/HTTPS transfers, server capability and range verification, segmented downloading, safe file writing, and integrity/fallback tests. No Phase 2 code has been implemented.

## Git checkpoint

- Published WIP checkpoint: `935f1e33e7033c866872a375abf5b8c5c0afbd06`.
- Remote push: verified on `dev/phase-01-foundation` by matching the remote ref.
- Earlier CI result: failed; https://github.com/ushalabs/Downshift-Download-Manager/actions/runs/38165976115. Local fixes have not been committed or pushed.
- The WIP commit and push occurred before the user changed the workflow. No further automatic commits or pushes are authorized.
- Next action: the user reviews and publishes the local changes, and explicitly approves Phase 2 when ready. The assistant must not commit or push automatically, and must not start Phase 2 before explicit approval. Current local tests passed; no successful rerun of the corrected CI workflow is claimed.

Use Git history to resolve the current document's containing commit; a commit cannot embed its own hash. Record earlier verified commit IDs here once available.
