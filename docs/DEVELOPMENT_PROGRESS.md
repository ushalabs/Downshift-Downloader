# Downshift development progress

## Phase gate

- Current phase: 1 - Native C++ Foundation.
- Last approved phase: 1, authorized by the supplied master prompt.
- Last completed phase: none.
- Status: incomplete foundation checkpoint, awaiting build/test verification.
- Product name: Downshift.
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

## Environment and verification

Initial local inspection on the client date 2026-10-11:

- Windows x64, reported version 10.0.26200.0.
- Git 2.55.0.windows.5 available.
- MSVC, CMake, Qt, and vcpkg not found in the inspected locations or PATH.
- GitHub connector authentication verified as ushalabs; authorized repository grants push access.
- Git transport works with the OpenSSL backend; the default transport stalled.
- Local compilation and CTest: not executed; toolchain is absent.
- Windows CI: prepared, not yet executed/verified.
- Benchmarks and IDM comparison: not executed; outside Phase 1.

## Decisions

Keep the required native Windows stack. Qt uses dynamic linkage, libcurl uses Schannel and HTTP/2, and SQLite uses its C API. HTTP/3 is optional and not selected currently. Download modules are directories reserved for later phases; no transfer engine or Chrome integration is implemented.

The vcpkg baseline and tool checkout are pinned to `e456309491fd875487bf02b8a0dca76c1b1b00cc`. Minimum CMake is 3.25; compiler/SDK versions will be recorded from the actual verified build.

## Remaining Phase 1 work

1. Compile and run all foundation tests on Windows with MSVC, locally or in the prepared CI job.
2. Fix any build/runtime defects and record exact results.
3. Create the verified phase commit and verify the GitHub push.
4. Report completion and stop for explicit Phase 2 approval.

## Git checkpoint

- Latest local commit: none at initial checkpoint.
- Remote push: pending.
- CI result: pending.
- Next action: validate the scaffold, commit a clearly labeled WIP checkpoint if necessary, and obtain a verified Windows build. Never declare Phase 1 complete from source inspection alone.

Use Git history to resolve the current document's containing commit; a commit cannot embed its own hash. Record earlier verified commit IDs here once available.
