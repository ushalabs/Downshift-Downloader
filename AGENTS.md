# Downshift contributor instructions

The product is **Downshift**, a native Windows C++20 application. Preserve the required MSVC, CMake, vcpkg, Qt 6 Widgets, libcurl multi, SQLite, and Chrome Manifest V3 stack.

## Phase gate

Latest direct authorization (2026-10-13) supersedes the historical restrictions below: implement content category folders with per-download overrides, strict Chrome capture, compact responsive pink UI, then Phase 9 packaging and Phase 10 release. Commit and push each verified stage to existing main; no further phase approval is required. Preserve user history, verify remote pushes/CI, and report actual limitations. Do not force-push or publish unverified artifacts. No delegated agent work is requested.

Read `docs/DEVELOPMENT_PROGRESS.md` and inspect Git status/history before changing code. Read `docs/PROJECT_REQUIREMENTS.md` for technical acceptance criteria. The user's latest direct instructions override the original attached prompt. Phases 1-8 are verified locally. The user subsequently approved Phase 8 and restored a manual Git workflow: implement and verify Phase 8, then provide instructions to create `main` and merge the development branch. Do not execute Git mutations for Phase 8. Phase 9 remains unapproved.

The active local project is `C:\Users\HP\Desktop\Projects\Downshift`. Use this directory for all project source and development artifacts.

Implement, build, test, fix, and document the approved phase. The earlier Phase 5-7 commits/pushes are complete. For Phase 8 leave changes uncommitted and give the user concrete Git instructions after verification. Do not create/switch/merge branches, commit, push, or publish a release. For interruptions, preserve files and record remaining work; do not mark incomplete phases complete.

Preserve existing history and user changes. Do not force-push, commit secrets, or include dependency/build directories. Record actual test results; distinguish failed and unexecuted checks. Do not fabricate benchmark, IDM, startup, or CI results.

The latest user request approves browser default takeover and video-player download controls before packaging. This is an additional development update after Phase 8, not Phase 9 authorization. Preserve the native file engine/queue; video extraction/merging uses pinned local helpers asynchronously in Qt windows. Helper setup is `scripts/Install-MediaTools.ps1` for each build configuration. `gui.media` skips explicitly if helpers are absent; execute it with helpers for media acceptance. All Git mutations still belong to the user.

Video tasks must persist in encrypted history and never enter the native file scheduler. A successful helper exit or completed stream is insufficient: verify the final saved local video before persisting Completed. Preserve retry folders and failed/interrupted records. Normal application and isolated Chrome test drivers share an exclusive user pipe; close the normal app gracefully before Chrome tests, preserve active downloads, and restore the registered host afterward.

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
