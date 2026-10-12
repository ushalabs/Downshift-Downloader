# Downshift development progress

## Phase gate

- Current phase: 8 - Performance Optimization.
- Last approved phase: 8; approved after the completed Phase 5-7 batch.
- Last completed phase: 8 - Performance Optimization (2026-10-13).
- Status: Phase 8 implemented and verified locally; changes remain uncommitted for the user’s manual Git handoff.
- Product name: Downshift.
- Active local project: `C:\Users\HP\Desktop\Projects\Downshift`.
- Former workspace copy remains as a backup; removing it was blocked by the execution approval policy. Do not develop in that copy.
- Git workflow: the latest user instruction reserves all Git operations for the user. Leave Phase 8 uncommitted and provide `main` creation/merge instructions after completion.
- Authorized repository: https://github.com/ushalabs/Downshift-Download-Manager.
- Phases 1-8: completed locally. Phase 9: not granted.

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

The user committed Phase 4 (`49bac63`) and authorized Phases 5-7 as a single batch. Phase 5 was pushed as `b90ca734e782628a06eb42fd7b27d97f5ce91491` and Phase 6 as `d1ccb51c1988abd2fa0be26af5f81afdb18754c6`, with each remote ref verified before continuing. Phase 7 is verified below; its containing commit is this batch's final checkpoint. Await Phase 8 approval after that push. Historical acceptance records preserve the workflow in effect when written.

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

## Phase 3 acceptance (2026-10-12)

Approved explicitly by "Start with phase 3". Implemented dynamic future chunk sizing, default automatic connection tuning, fixed 1-16 limits including 1/4/8/16, speed smoothing/ETA, safe worker progress callbacks, and structured CPU/memory/file/network timing measurements. Completed ranges remain immutable; fast workers receive new ranges as they finish. Automatic throughput trials retain useful expansions, roll back plateaus, and reduce future assignments under sustained loss or write pressure. Streaming retains one transfer when segmentation is unsafe.

Same verified Windows/MSVC/SDK/pinned dependency environment as earlier phases. Both configurations built without compiler warnings/errors; no additional system installation or dependency revision changes.

| Check | Release | Debug |
|---|---|---|
| GUI, client, engine, and test compilation | Passed | Passed |
| `engine.unit` (8,040 assertions) | Passed (0.07 s) | Passed (0.19 s) |
| `engine.integration` (52 HTTP/HTTPS scenarios) | Passed (5.95 s) | Passed (6.17 s) |
| `engine.adaptive` (12 scenarios) | Passed (18.31 s) | Passed (18.16 s) |
| `foundation.dependencies` | Passed (0.04 s) | Passed (0.05 s) |
| `foundation.gui.minimal` | Passed (0.32 s) | Passed (0.36 s) |
| `foundation.gui.windows` | Passed (0.45 s) | Passed (0.55 s) |
| CTest summary | 6 passed, 0 failed; 25.16 s | 6 passed, 0 failed; 25.52 s |

Tests independently verify file SHA-256 and size, exact contiguous range coverage, hard concurrency limits, manual stability, automatic growth and rollback, heterogeneous worker scheduling, progress/rate/ETA values, and observer exception isolation. Deterministic native units cover sustained loss/resource-pressure reductions, latency/chunk bounds, and repeated scheduling changes beyond 8 GB. Existing Phase 2 protocol/TLS checks retain their fixed 4-slot/4 MiB baseline.

Final review corrected pending-header streaming progress: the range probe's one-byte Content-Length is never exposed as the full file size. Its first regression assertion was too strict about a zero-byte snapshot after full headers arrived; corrected the expectation to allow the verified full size at that point. The final 12-scenario adaptive group passed in both configurations.

Benchmark acceptance: 90 samples in `benchmarks/results/phase3-local-20261012.{json,csv,md}`, covering 100 MB/1 GB/5 GB, fixed 1/4/8/16, automatic and streaming, with three repetitions per group. The 100 MB cases also cover per-connection and shared caps with delayed headers. All native checksums, independent reread checksums/sizes, range coverage, and hard-limit checks passed. The main matrix predates only the pending-header display correction; exact source bytes are preserved and hash-verified in `phase3-local-20261012-source.json`. The final Release executable separately passed 18 verified 100 MB unlimited samples in `phase3-final-progress-20261012.{json,csv,md}`. Total: 108 accepted benchmark samples. The supplement's source and executable fingerprints match the final local files.

Main results: automatic mode's 100 MB per-connection-capped median was 23.44 MiB/s end-to-end versus fixed 4 at 13.58 MiB/s; fixed 16 was faster at 36.01 MiB/s. Shared-cap rates plateaued near 14.8 MiB/s. Automatic was slower than fixed 4 on 1 GB unlimited loopback (213.87 versus 289.35 MiB/s), and the uncapped repetitions varied substantially. Maximum native-client peak working set was 7.51 MiB in the main matrix. Full results, raw CPU/write/setup measurements, source provenance, and interpretation limits are in [benchmarks](BENCHMARKS.md).

Acceptance: correct adaptive scheduling with measured throughput and no missing/overlapping segments. No local Phase 3 acceptance blocker remains. Automatic mode is a conservative heuristic rather than a globally optimal policy. Timing measures cache-enabled HTTP/1.1 loopback with a server on the same machine; no physical disk bandwidth, Internet/HTTP2 wire performance, or IDM superiority is claimed. Full desktop controls remain Phase 6. Phase 3 CI has not been run; manual publication remains separate.

Native builds and verbose logs remain under `build/windows-msvc` and `build/phase3-{release,debug}-{build,tests}.log`; benchmark logs and transient payloads are ignored. Verified benchmark reports and the measured-source archive are intentionally retained as source-review evidence. No commits/pushes were made by the assistant.

## Phase 4 acceptance (2026-10-12)

Approved explicitly by "Continue with 4". Implemented worker/console pause, explicit resume/restart, durable encrypted range metadata, local byte/file-identity validation, conditional remote revalidation, bounded transient retries with exponential jitter/Retry-After, and recovery across forced termination and finalization gaps. Ctrl+C/Ctrl+Break requests pause; a new worker/process resumes missing intervals. The GUI remains the foundation shell. No SQLite queue/history/settings or Phase 5 work is implemented.

Recovery ledgers persist IDs, original/resolved source URLs, destination, expected size/checksum, file identity, ETag, Last-Modified, timestamps, capability/status, and completed range digests. User-scoped DPAPI encrypts the whole ledger, including URL query secrets. Completed bytes are flushed before atomically publishing the ledger. Reuse requires matching local identity/size/hashes and remote strong validator/length/location. Invalid or unverifiable partials are preserved; explicit restart archives them under unique names. Completed destinations are never overwritten. Finalization reconciliation verifies a renamed file before clearing a stale ledger.

Same verified Windows/MSVC/SDK/pinned dependency environment as previous phases. Builds passed without compiler warnings/errors, with no new system installation or dependency updates. Python 3.12.14 remains a test/benchmark dependency only.

| Check | Release | Debug |
|---|---|---|
| GUI, client, engine, and test compilation | Passed | Passed |
| `engine.unit` (12,225 assertions) | Passed (0.13 s) | Passed (0.15 s) |
| `engine.integration` (52 HTTP/HTTPS scenarios) | Passed (5.08 s) | Passed (5.47 s) |
| `engine.adaptive` (12 scenarios) | Passed (17.66 s) | Passed (17.74 s) |
| `engine.recovery` (36 scenarios) | Passed (21.64 s) | Passed (23.06 s) |
| `foundation.dependencies` | Passed (0.02 s) | Passed (0.06 s) |
| `foundation.gui.minimal` | Passed (0.29 s) | Passed (0.38 s) |
| `foundation.gui.windows` | Passed (0.36 s) | Passed (0.42 s) |
| CTest summary | 7 passed, 0 failed; 45.19 s | 7 passed, 0 failed; 47.30 s |

Recovery checks independently verify final size/SHA-256 and combine saved/new request ranges to establish complete nonoverlapping coverage without refetching saved ranges. Actual forced process termination and competing-worker rejection passed. Repeated pauses, changed remote validators/size/Last-Modified/range capability, changes during resume, corrupted/replaced/truncated/hard-linked partials, malformed ledgers, mismatching URL/checksum, streaming restart, and legacy partial archival all passed. Fully saved partials and interruption after rename reconcile without fetching completed ranges; damaged completed destinations are rejected. Native checks reopen a sparse file and verify a range above 4 GB, and schedule gaps around a saved range above that boundary.

Retry checks passed for transient probe/range HTTP failures, probe timeout, dropped segment/stream bodies, useful saved work after exhaustion, Retry-After, excessive server delays, permanent authorization/precondition errors, TLS verification failure, and pause during backoff. Native arithmetic checks bound exponential/jitter delays. Existing Phase 2 integration errors run with zero retries to retain their original regression baseline; the recovery group exercises the normal retry policy.

During verification, corrected the encrypted-fixture mutation to use an existing-file handle (Windows hidden files rejected the test's truncating open), and allowed the console checkpoint aid to pause streaming by written bytes because streaming has no completed range counter. Those initial failures were resolved before both full passing suites. Final review also validates destination paths before any sidecar operations and distinguishes already-renamed file inspection from partial reopening.

Performance check: `benchmarks/results/phase4-local-20261012.{json,csv,md}` contains 18 accepted Release samples: 100 MB unlimited HTTP/1.1 loopback, fixed 1/4/8/16, automatic, and streaming, three repetitions each. Native/independent SHA-256, sizes, coverage, and concurrency limits all passed. Median end-to-end rates: fixed 4 at 232.80 MiB/s, automatic at 221.88 MiB/s, streaming at 396.98 MiB/s. This measures fresh transfers including checkpoint overhead, not interrupted/retried throughput or a controlled cross-phase comparison. Source/executable fingerprints identify this build. An initial identical-size run with stale Phase 3 report labels was retained under ignored `build/phase4-benchmark-initial`; corrected report metadata preceded the final recorded run. Earlier Phase 3 results remain historical evidence.

Acceptance: supported interrupted downloads safely resume without corrupting or combining versions, with bounded retries and verified durable ranges. No local Phase 4 acceptance blocker remains. Weak/missing ETags and streaming partials require explicit restart; same-user DPAPI access, cooperative hashing/flush latency, and bounded ledger sizes are documented in [recovery](RECOVERY.md). Actual Windows reboot, physical power loss, disk-full injection, HTTP/2 wire recovery, full desktop controls, and CI are not claimed as tested.

Logs are `build/phase4-{release,debug}-{build,tests}.log` and `build/phase4-benchmark.log`; generated dependencies, payloads, caches, and logs are ignored. Verified benchmark reports and source changes are ready for the user's manual commit/push. No assistant Git commits or pushes were made.

## Git checkpoint

- Latest user-created commit: `49bac63` (`Phase 4`); working tree was clean at Phase 5 start and matched the remote branch.
- Earlier user-created commit: `d7d9a1e55320bf4ddb304e58ae6b807775d1a823` (`Phase 3`).
- Earlier user-created commit: `b70626c5931be10f5555c9de8183b69c961a78c2` (`Phase 2`).
- Earlier user-created commit: `8dddb3c47d0c21ed729ac8c165eff1d197612dc0` (`Phase 1`).
- Published WIP checkpoint: `935f1e33e7033c866872a375abf5b8c5c0afbd06`.
- Remote push: verified on `dev/phase-01-foundation` by matching the remote ref.
- Earlier inspected CI result: failed; https://github.com/ushalabs/Downshift-Download-Manager/actions/runs/38165976115. The user subsequently committed Phase 1 locally. No Phase 2 CI run has been executed by the assistant.
- The WIP commit and push occurred before the earlier manual workflow. The latest user request now authorizes assistant commits and pushes after each verified Phase 5-7.
- Phases 2 and 3 were left uncommitted at their handoffs; the user subsequently committed them. No assistant commits/pushes were made during Phases 2-4.
- Assistant phase checkpoints: Phase 5 `b90ca734e782628a06eb42fd7b27d97f5ce91491`, Phase 6 `d1ccb51c1988abd2fa0be26af5f81afdb18754c6`; both pushed to `dev/phase-01-foundation` and remotely verified.
- Phase 7 checkpoint: `017390e3167f0f6dad40f2671ae29f8f894d456a`, matched to the remote development branch at Phase 8 start. Phase 8 is now approved with all Git operations reserved for the user. No new successful CI result is claimed.

Use Git history to resolve the current document's containing commit; a commit cannot embed its own hash. Record earlier verified commit IDs here once available.

## Phase 5 acceptance (2026-10-12)

Approved together with Phases 6-7, including assistant commits/pushes after each verified phase. Added the C++ queue service, encrypted SQLite queue/history/settings and recovery references, stable priorities, global and original-origin download/connection limits, global/per-download byte pacing, pause/resume/cancel/restart/remove/rename operations, exclusive database ownership, crash/startup holding, and bounded progress mailboxes. Database work and engine workers stay outside the GUI thread; the desktop remains the foundation shell until Phase 6.

Both MSVC x64 configurations compiled without warnings/errors. All eight CTest groups passed in Release (62.97 s) and Debug (63.69 s). The new real-transfer queue group passed in 18.95/18.25 s, respectively; engine protocol, adaptive, recovery, dependency, and both GUI startup regressions also passed. Queue evidence includes independently hashed concurrent files, priorities, origin/global bounds, aggregate pacing, encrypted DB/WAL query secrets, settings/history across restarts, ownership refusal, forced process death followed by resume, pause/cancel preserving partials, non-destructive removal, invalid-input rejection, and authenticated-record corruption failure.

Initial failures were in fixtures: a query string accidentally disabled test-server ranges, and Python's SQLite context manager left a corruption-test connection open during directory cleanup. Corrected both and added unconditional child-process cleanup before the final passing runs. Durable queue save failures stop scheduling. Range journals remain the authority for actual verified recovery intervals. Limits apply to the submitted origin; independent redirects do not share a global target-host reservation. Global and per-download caps can conservatively underutilize bandwidth when combined. No new performance or CI result is claimed.

Acceptance: queues/history/settings persist, multiple real downloads are scheduled reliably, and interruptions preserve recoverable work. Logs: `build/phase5-{release,debug}-{build,tests}.log`. This phase was committed/pushed as `b90ca734e782628a06eb42fd7b27d97f5ce91491` before Phase 6 began.

## Phase 6 acceptance (2026-10-12)

Added the real Qt desktop: light-default/dark appearance, native branding, live download table, progress/rates/ETA/connections/status/priority, selected details, aggregate speed, add/start/pause/resume/cancel/restart controls, search/filters/history, filename changes, copy link/open folder, and explicit confirmed open/delete actions. Settings persist folder, concurrency/connection limits, speed, appearance, startup queue behavior, notifications, and redacted activity logging. Completed history retains average rate/elapsed time. All queue/database/file/engine operations remain on workers; GUI snapshots and command results are asynchronous and bounded. Closing cooperatively pauses workers.

Both MSVC configurations compiled without warnings/errors. All nine CTest groups passed across the full regression and final targeted runs in each configuration. The full Debug suite passed in 79.90 s. The initial full Release run passed eight groups but its GUI heartbeat assertion included initial widget construction while a Debug build was consuming CPU. Scoped that assertion to the actual download interval and reran the changed GUI/queue groups in both configurations. Final targeted Release: 2/2 passed, 31.92 s; Debug: 2/2 passed, 33.12 s. GUI checks: 19 each, including an independently reread SHA-256, pause/resume, details/search/filters, settings Save/Cancel, actual light/dark renders, validation feedback, history/settings/metrics reopening, and worker deletion. Longest observed transfer event gap: 0.0316 s Release and 0.1522 s Debug. Queue checks: 26 each, including live aggregate-cap changes waking a long wait and redacted diagnostic logs. Existing engine/TLS/recovery/dependency/Windows/minimal GUI checks passed in the full runs; no further engine change followed them.

Render review corrected the aggregate-speed label width and provided validation feedback inside Add Download. Qt's pinned minimal feature set does not encode PNG; test renders use built-in BMP and the committed PNG screenshots are conversions of real captures. Visually reviewed both actual themes. The test loop explicitly drains deferred widget deletion. Files/screenshots are in the Desktop project; no mockup, Internet-speed, startup benchmark, or CI result is claimed.

Acceptance: the functional desktop operates the real queue/engine while remaining responsive during transfers. Usage/security and shutdown behavior: `docs/DESKTOP.md`. Logs: `build/phase6-{release,debug}-{build,tests}.log` and `build/phase6-controls-{release,debug}-tests.log`. This phase was committed/pushed as `d1ccb51c1988abd2fa0be26af5f81afdb18754c6` before Phase 7 began. Installer/distribution remains later.

## Phase 7 acceptance (2026-10-12)

Implemented Manifest V3 download events/controls, default-disabled consent, a popup with connection/open/restore/retry controls, a separate origin-restricted Native Messaging host, and cancellable bounded Windows named-pipe IPC restricted to the current user. Native preflight validates public range/strong-ETag representations. Encrypted SQLite schema 2 migrates existing queues and atomically saves prepared entries with receipts. Chrome cancellation is verified before commit; replayed prepare/status/commit/abort messages remain idempotent, including after history removal. Prepared work cannot start through generic desktop controls. Engine transfers pin preflight ETag, resolved URL, and size. GUI/database/network ownership stays on the existing asynchronous worker boundaries.

Both MSVC x64 configurations built without compiler warnings/errors, using the unchanged pinned dependency set. Final full suites:

| Check | Release | Debug |
|---|---|---|
| `browser.extension` (29 controller checks) | 0.06 s | 0.05 s |
| `browser.native` (27 real protocol/queue checks) | 2.66 s | 3.14 s |
| `gui.integration` (19 controls/checksum checks) | 12.98 s | 13.59 s |
| `queue.integration` (26 checks, plus schema migration) | 18.89 s | 19.09 s |
| `engine.unit` (12,225 checks) | 0.11 s | 0.10 s |
| `engine.integration` (52 HTTP/HTTPS scenarios) | 4.28 s | 4.75 s |
| `engine.adaptive` (12 scenarios) | 17.68 s | 17.66 s |
| `engine.recovery` (36 scenarios) | 21.48 s | 22.58 s |
| Dependency / minimal GUI / Windows GUI | All passed | All passed |
| CTest summary | **11 passed, 0 failed; 80.45 s** | **11 passed, 0 failed; 83.68 s** |
| Separate actual Chrome for Testing checks | **9 passed** | **9 passed** |

GUI transfer heartbeat's longest observed gap was 0.0278 s Release / 0.0783 s Debug. Registration, valid manifests for both configurations, unregister, PowerShell/Python syntax, JSON, and whitespace checks passed. The registry check and browser tests restored the earlier registration; no extension was installed into the normal browser profile.

Actual Chrome for Testing 155.0.8059.39 with Playwright 1.62.1 loaded the real extension in an isolated profile and exercised the actual Native Messaging registry/host, Qt window, queue, pipe, and engine. Both native completions and browser fallback files independently matched SHA-256 `2b07811057df887086f06a67edc6ebf911de8b6741156e7a2eb1416a4b8b1b2e`. Checks cover identity/readiness/default-disabled consent, disabled passthrough, a public handoff, duplicate reconcile, actual Chrome/worker restart during preparation, safe abort of the test-browser's cancelled download, a successful new handoff after restart, cookie-only authentication fallback, and native-unavailable fallback. Closing the automated browser also cancels its live download; this actual restart test verifies safe abort, not a standalone worker-only termination. Deterministic controller tests separately verify worker reconstruction, cancellation failures/races, lost commit acknowledgement, and outage after cancellation. Native tests verify no file before commit, durable preparation across app restart, abort stability, receipt retention after removal, invalid framing/origins/filenames/sizes, validator change rejection, and encrypted records. Evidence: `docs/evidence/phase7-*.json` with source/executable fingerprints; an actual popup capture is `assets/screenshots/chrome.png`.

Resolved test-environment failures before these passing results: native IDM integration intercepted the isolated Chrome executable before Downshift; an identical signed Chrome for Testing copy with a test-only process name avoided that hook without changing IDM settings. Initial test queries selected an old cancelled download, and a service-worker message incorrectly sent to itself; corrected the harness. Inspector stop/reload attempts did not establish the expected replacement worker, so the accepted test instead restarts actual Chrome and explicitly verifies the resulting safe abort. Transient failed diagnostics remain under ignored `build`; accepted evidence is separate. No IDM performance comparison or new CI success is claimed.

Acceptance: supported public downloads transfer successfully from actual Chrome into the native application, with tested failures and duplicate-safe recovery. Limits: browser cookies/authentication are not forwarded; the original browser ETag/checksum is unavailable, so same-length personalized/public representations cannot be proven equivalent. Unsupported/private resources should stay in Chrome. After cancellation an unavailable native receipt holds the decision pending until reconnect, avoiding an uncertain duplicate. Receipts are retained with a 10,000 bound. Same-user software is outside the Windows account protection boundary; remote/foreign-user rejection is enforced in code but not exercised with a second account. These are controlled HTTP tests, not universal website, HTTP/2 wire, public TLS-browser, throughput, reboot/power-loss, installer, or release claims. Setup, recovery, and precise boundaries are in [Chrome integration](CHROME.md) and [testing](TESTING.md).

Logs: `build/phase7-{release,debug}-{build,tests}.log` and `build/phase7-chrome-{release,debug}.log`. Commit/push this final verified phase to the existing branch; its own hash is resolved from Git history. After remote verification, all three approved phases are finished and Phase 8 awaits the user's approval.

## Phase 8 verification (2026-10-13)

Approved separately after Phase 7. All source, builds, test files, and reports remain in `C:\Users\HP\Desktop\Projects\Downshift`. The user reserves all Git operations and requested instructions for a new `main` branch and merge after completion. No staging, commit, branch, merge, or push operation has been performed during this phase. Phase 9 remains unapproved.

Implemented bounded 64 KiB validated/paced segment-write buffers, a reusable 1 MiB checksum-read buffer, requested 256 KiB segmented libcurl receives, and write/read/checkpoint counters. Streaming retains the established default receive/direct-write path after a wider-receive prototype regressed. Retry progress subtracts actual writes; successful verified responses alone flush short segment tails. Independent range/final checksums, flush-before-journal ordering, TLS/validator/length checks, and queue limits remain intact.

Long prototype benchmarks exposed an existing metadata-replacement failure in both original Phase 7 and optimized clients. An isolated operation-specific diagnostic confirmed Windows error 5 during replacement; the external blocking cause is unknown. Production retries only relevant replacement errors with nine attempts/eight 25 ms waits (200 ms of requested waits; filesystem calls and scheduling can add time), retaining the prior encrypted ledger on exhaustion. Real Windows reader-lock tests verify transient recovery, permanent safe failure, encrypted checkpoint preservation, and temporary-file cleanup. Final performance comparisons apply this identical journal hardening to the Phase 7 comparison engine to isolate the I/O changes. See [performance decisions and provenance](PERFORMANCE.md).

Both MSVC x64 builds compiled without warnings/errors. Final full CTest: Release **12/12 passed, 102.03 s**; Debug **12/12 passed, 105.61 s**. Each configuration passed 12,239 engine checks, 52 HTTP/HTTPS integration scenarios, 12 adaptive scenarios, 22 buffered/tail/retry transfers, 36 recovery scenarios, 26 queue checks, 19 desktop checks, 27 native browser checks, 29 extension checks, dependency checks, and two GUI startup checks. GUI longest event gaps were 0.068059 s Release / 0.210436 s Debug. Separate actual Chrome for Testing: **9 checks each**, with independently hashed native and browser files. Evidence: [local tests](evidence/phase8-local-tests.json), [Release Chrome](evidence/phase8-chrome-release.json), [Debug Chrome](evidence/phase8-chrome-debug.json). Earlier prototype test evidence is preserved separately under `phase8-wide-*`.

An earlier Debug desktop check observed a non-running status after its progress threshold; the original assertion did not report status/error. Added diagnostic output without changing the product or weakening the check. A focused rerun and subsequent complete suites passed; no external cause is inferred. Its failure is retained in [verification notes](evidence/phase8-debug-desktop-observation.json) rather than described as a clean first run.

An early Chrome restart test closed the browser after its storage stage became preparing but before a native queue entry existed. The corrected harness holds a test-only browser storage transition after real native prepare acknowledgment, asserts a real Prepared entry, then closes Chrome before cancellation authorization advances. The gate dies with the old worker; the replacement uses unchanged real APIs. No product extension code changed. Final tests restored the original absent HKCU registration and left the normal profile untouched.

Preserved preliminary performance evidence: 36 initial diagnostic samples, 36 first-pilot samples, 18 receive-pilot samples, and incomplete prototype runs of 137 and 39 samples. These prototypes are excluded from final acceptance. Installed IDM 6,42,63,3 did not request the local fixture or produce a file in either final documented-CLI availability probe. Probe-owned launchers were closed; no throughput comparison or licensing/configuration cause is inferred. See [IDM status](IDM_COMPARISON.md).

Final matrix: **180 accepted samples, 90 pairs, 30 cases, three pairs each; zero failed attempts.** All sizes/hashes, exact byte totals/range coverage, hard connection bounds, current binary/source fingerprints, and archive hashes passed. No interrupted/prototype subsets are combined. [Final report](../benchmarks/results/phase8-final-20261013.md), [acceptance evidence](evidence/phase8-performance-acceptance.json), and [measured source archive](../benchmarks/results/phase8-final-source.json) retain reproducible results. The full report includes gains and regressions; no universal speed/CPU or IDM advantage is claimed. Phase 8 meets its verified-benchmark/documented-decisions/correctness criteria. Git HEAD remains the Phase 7 checkpoint; all phase changes remain uncommitted for the user. [Manual Git instructions](GIT_HANDOFF.md) are prepared for the completed handoff. No new CI, physical-network/storage, HTTP/2 wire, installer, or release result is claimed.
