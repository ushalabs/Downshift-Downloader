# Downshift contributor instructions

The product is **Downshift**, a native Windows C++20 application. Preserve the required MSVC, CMake, vcpkg, Qt 6 Widgets, libcurl multi, SQLite, and Chrome Manifest V3 stack.

## Phase gate

Latest direct authorization (2026-10-13) supersedes the historical restrictions below: implement content category folders with per-download overrides, strict Chrome capture, compact responsive pink UI, then Phase 9 packaging and Phase 10 release. Commit and push each verified stage to existing main; no further phase approval is required. Preserve user history, verify remote pushes/CI, and report actual limitations. Do not force-push or publish unverified artifacts. No delegated agent work is requested.

Read docs/DEVELOPMENT_PROGRESS.md, docs/PROJECT_REQUIREMENTS.md and Git status/history before edits. Latest direct authorization supersedes earlier manual Git and phase gates recorded in those documents. Downloader and Phase 9 changes are pushed to main; Phase 10 requires successful current Windows CI and verified clean source/artifacts before publishing v0.3.0. Preserve history/user files, do not force push or fabricate results. Use the Desktop project directory.

Media tasks must persist encrypted and stay outside the native scheduler. Completion requires the actual final local video and container/stream verification; preserve retry folders and interrupted records. Media helpers are installed for real media tests but are not redistributed in the portable archive. The normal app and isolated Chrome drivers share an exclusive pipe: close an idle normal app gracefully before browser tests, preserve active downloads and restore registration afterward. Opening a download requires explicit confirmation; downloads never execute automatically.

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
