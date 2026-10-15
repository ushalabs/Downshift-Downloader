# Downshift contributor instructions

The product is **Downshift**, a native Windows C++20 application. Preserve the required MSVC, CMake, vcpkg, Qt 6 Widgets, libcurl multi, SQLite, and Chrome Manifest V3 stack.

## Phase gate

Latest direct request (2026-10-15, pre-T4 GUI/image repair): correct the white GUI, navigation spacing, selection outline, redundant bars, rounded dropdowns, simple deletion confirmation, real thumbnails, left headings and detail close/collapse controls. Fix Chrome image capture into Pictures without re-encoding. Earlier T2/T3 approval is complete; T4 still requires explicit approval. Keep all Git operations manual: no stage, commit, push, branch or publish. Build the complete change, verify at the end, then fix actual failures. No delegated work. Preserve the current real queue/preferences/completed files and paused tasks; tests use hidden private queues and the separate test host/pipe. Cleanup may remove only authenticated owned incomplete data from cancelled/failed tasks, never completed destinations, paused data or an unverified sole received browser copy. Normal extension reload remains manual. Older phase gates below are historical.

Latest direct request (2026-10-15): fix the historical Chrome-download popup flood and YouTube quality lookup/retry failure for vwLAqU1HvQ8. Keep the work local with no Git mutations or torrent work. Do not import completed Chrome history from metadata/restoration events. Preserve existing files/history/paused tasks; do not bulk-delete the accidental records. Verify fresh tiny captures and restart recovery independently. The supplied video's full extractor JSON exceeds the old 4 MiB limit; use a compact quality summary and verify the actual selected video through the native dialog.

Latest direct request (2026-10-15, superseding the narrower HTTPS repair below): implement general browser-response delivery, including blob/data URLs, authenticated GET and POST downloads, rather than leaving unsupported handoffs in Chrome. This is the local 0.4.3 browser bridge. Preserve the existing SQLite file/schema/settings/history and torrent changes; queue adapters may use optional encrypted JSON fields for the new transport. Do not commit, push, publish or work on torrents. Tests must use private queues, the separate test host/pipe and generated data. Public responses can use curl only when the browser representation's strong validator matches; other responses retain their original browser transport and finish through native verification. Never erase the only received copy on Cancel/history removal. Report the real browser-transport/storage costs and verification limits; do not claim proprietary IDM internals were reproduced or that every internal browser operation can be intercepted.

Latest direct request (2026-10-15): fix Chrome capture/direct HTTPS handoff only. Do not work on torrents, change the database implementation or user data, commit, or push. Existing torrent source/test modifications belong to other work and must remain untouched. Enforce enabled capture after browser resume/restart and for tiny attachments, preserve filenames, verify the supplied public Roblox installer through isolated Chrome, and report unsupported browser-only blob behavior honestly. Normal-profile UI automation stopped because its current URL could not be determined reliably; do not bypass that guard. Reloading the extension/refreshing existing pages remains a manual application step unless safely completed later.

Latest direct requests (2026-10-14): complete the recurring Desktop history repair (0.4.2), preserving the six-record encrypted queue, preferences and partials. Confirm the physical store across Explorer/development launch contexts rather than relying on displayed paths. Then implement only Torrent Phase T1 from the user-provided BitTorrent brief: native Rasterbar libtorrent foundation, build/tests/docs, commit/push main, report and STOP for explicit T2 approval. No T2 persistence or T3 GUI work is authorized by that brief. No delegated agent work was requested.

Latest direct request (2026-10-14, after GUI review): investigate IDM/Downshift throughput and implement a general performance repair. This authorizes the dedicated 0.4.1 transport follow-up and verified main commit/push. Preserve the user's actual queue/settings/partial files. Initial inspection for this follow-up found six records and no global/per-task speed caps. Do not reset preferences, install machine/router traffic-priority settings or claim a universal IDM/ISP speed guarantee. Validate independent physical connections over real HTTP/2, reuse and caps, background contention, full correctness/recovery suites and a bounded same-source live observation. Keep test downloads separate; do not publish a new release or start another phase automatically. Earlier GUI stop was satisfied before this new request.

Current direct request (2026-10-14): prioritize the apparent missing history after Desktop restart, then complete the user-referred GUI 2.0 brief as one dedicated phase. Preserve the actual database, paused task, partials and preferences. Initial inspection found seven records; the user subsequently confirmed deliberately removing one failed record, so the current six-record state must be preserved. v0.3.3 was verified/published on source 41a71a3 and is immutable; this work uses 0.4.0. Commit/push the verified phase to main, report the result and stop for user review; do not start another phase or publish 0.4.0 without that review. No delegated work is requested. All test windows must use private queues and remain hidden, including GUI/media and smoke checks; never present a temporary test queue as the normal app. Distinguish confirmed data integrity and reproduced misleading loading/test behavior from the unobserved exact trigger of the user's original empty window.

Latest direct authorization (2026-10-13) supersedes the historical restrictions below: implement content category folders with per-download overrides, strict Chrome capture, compact responsive pink UI, then Phase 9 packaging and Phase 10 release. Commit and push each verified stage to existing main; no further phase approval is required. Preserve user history, verify remote pushes/CI, and report actual limitations. Do not force-push or publish unverified artifacts. No delegated agent work is requested.

Follow-up repair: fix the actual YouTube failure/quality resolution before UI polish. Watch pages must never run as native video-file downloads; reject unresolved feed/playlist metadata and retain file/history evidence. Verify a real full public YouTube transfer as well as controlled regressions. Keep a new corrected preview separate from the immutable historical v0.3.0 release. The app icon must work in both Windows executable resources and Qt without assuming optional image plugins.

Second repair: prioritize the confirmed extension-overlay failure, direct Chrome capture, source-speed diagnosis and partial-range Pause/Resume. Preserve the user's paused WinRAR task and partial. Do not claim recovery of previously uncheckpointed bytes, universally suppressed Chrome events, or ISP throughput from a slow source. The 0.3.2 preview is verified and immutable. The current generic compatibility/UI repair uses v0.3.3, preserving all earlier previews.

Read docs/DEVELOPMENT_PROGRESS.md, docs/PROJECT_REQUIREMENTS.md and Git status/history before edits. Latest direct authorization supersedes earlier manual Git and phase gates recorded in those documents. Downloader and Phase 9 changes are pushed to main; Phase 10 requires successful current Windows CI and verified clean source/artifacts before publishing v0.3.0. Preserve history/user files, do not force push or fabricate results. Use the Desktop project directory.

Media tasks must persist encrypted and stay outside the native scheduler. Completion requires the actual final local video and container/stream verification; preserve retry folders and interrupted records. Media helpers are installed for real media tests but are not redistributed in the portable archive. Browser/native tests now use a separate test host, per-user test pipe, hidden windows and private queues; do not modify normal Chrome registration/profile. Close an idle normal app gracefully only when rebuilding its EXE, preserving active downloads. Opening a download requires explicit confirmation; downloads never execute automatically.

Binary distributions require the actual license notices, complete matching library sources/patches/recipes, permitted Qt library replacement and corresponding application source. Use Build-Portable.ps1 -RequireClean and Test-Portable.ps1. Report successful local, remote CI and release verification separately; an unsigned portable preview does not imply installer/store signing or clean-VM testing.

## Foundation build and checks

From the repository root in PowerShell, after installing MSVC v143, Windows SDK, CMake 3.25+, Git, and Python 3.10+ for the integration servers:

```powershell
./scripts/Bootstrap.ps1
./scripts/Test-Environment.ps1
cmake --preset windows-msvc
cmake --build --preset release --parallel 2
ctest --preset release
cmake --build --preset debug --parallel 2
ctest --preset debug
```

The vcpkg checkout must match the immutable manifest baseline. Use `x64-windows` for shared Qt/runtime linkage. Do not weaken TLS verification to work around tool downloads. A local startup check and a verified Windows CI run are separate evidence; report which environment executed each check.

## Design constraints

Use RAII and explicit ownership. Never run blocking network, disk, or persistent database work on the GUI thread. File correctness and recovery take priority over speed. Native Messaging will use a separate host with restricted extension origins and secure local IPC. Store no credentials in plaintext, and log no secrets. Never automatically execute downloads.

Qt LGPL compliance and dependency notices must be completed before binary distribution. Source-development CI does not constitute an installer or release. Keep Markdown whitespace-clean and product-facing text clear.
