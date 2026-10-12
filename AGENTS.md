# Downshift contributor instructions

The product is **Downshift**, a native Windows C++20 application. Preserve the required MSVC, CMake, vcpkg, Qt 6 Widgets, libcurl multi, SQLite, and Chrome Manifest V3 stack.

## Phase gate

Read `docs/DEVELOPMENT_PROGRESS.md` and inspect Git status/history before changing code. Read `docs/PROJECT_REQUIREMENTS.md` for technical acceptance criteria. The user's latest direct instructions override the original attached prompt. Implement only the phase explicitly approved by the user. Phase 1 is approved; later phases require separate explicit approval. Silence and previous-phase approval do not authorize advancing.

The active local project is `C:\Users\HP\Desktop\Projects\Downshift`. Use this directory for all project source and development artifacts.

Within an approved phase, work independently: implement, build, test, fix, document, report results, and stop to request the user's approval for the next phase. The user handles Git commits and GitHub pushes. Do not commit, push, merge, or publish automatically. Leave phase changes in the working tree for the user. Local phase completion requires its acceptance checks; manual publication is a separate user action. For interruptions, preserve files and record remaining work without committing.

Preserve existing history and user changes. Do not force-push, commit secrets, or include dependency/build directories. Record actual test results; distinguish failed and unexecuted checks. Do not fabricate benchmark, IDM, startup, or CI results.

## Foundation build and checks

From the repository root in PowerShell, after installing MSVC v143, Windows SDK, CMake 3.25+, and Git:

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
