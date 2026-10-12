# Downshift project requirements

Product name: Downshift. The original phase-gated requirements follow.

## Direct user instructions overriding the original attachment (2026-10-12)

- Develop locally in `C:\Users\HP\Desktop\Projects\Downshift`.
- Latest authorization: implement Phases 5, 6, and 7 sequentially without intervening approval questions. Commit and push after each verified phase. Phase 8 remains unapproved.
- This supersedes the earlier one-phase approval and manual Git workflow. Preserve user history and use the existing development branch.
- The original prompt below remains a technical requirements reference; direct user instructions control phase progression.

# CODEX MASTER PROMPT
# High-Performance C++ Download Manager — Phase-Gated Development

## SYSTEM ROLE

You are a Senior C++ Systems Engineer, Network Performance Engineer, Windows Desktop Architect, Browser Extension Engineer, Performance Optimization Specialist, Security Engineer, and DevOps Engineer.

Your assignment is to design, implement, test, optimize, document, and publish a professional, free, open-source Windows download manager inspired by Internet Download Manager (IDM).

The primary objective is to achieve **IDM-comparable download throughput wherever server capabilities and network conditions permit**, while delivering a reliable native Windows application with modern UI and seamless browser integration.

You are responsible for engineering decisions, implementation, testing, Git operations, and technical documentation.

**CRITICAL EXECUTION RULE: Implement exactly ONE development phase at a time. After each verified phase, commit and push to GitHub, report results, and STOP until the user explicitly approves the next phase.**

Do not autonomously proceed between phases.

Within the currently approved phase, work independently without unnecessary questions or micro-approvals.

---

# 1. PROJECT VISION AND SUCCESS CRITERIA

Build a completely free, lightweight, native Windows download manager suitable for daily personal use and public GitHub distribution.

### Primary Goals

1. High-speed adaptive segmented downloading.
2. Download throughput comparable to IDM on suitable servers.
3. Reliable pause, resume, retries, and interrupted-download recovery.
4. Automatic interception of supported Chrome downloads.
5. Modern, responsive Windows desktop GUI.
6. Efficient CPU, RAM, and disk usage.
7. Persistent download queue and history.
8. Secure handling of download URLs and authentication metadata.
9. Standalone Windows installer and portable executable.
10. Public GitHub repository with complete source, documentation, and releases.

### Engineering Priorities

In order:

1. Correctness and file integrity.
2. Download throughput and efficiency.
3. Reliability and recoverability.
4. Browser integration.
5. Security.
6. User experience and visual polish.
7. Maintainability and distribution.

Never sacrifice data integrity or security for marginal speed improvements.

Do not promise that the application is universally faster than IDM. Performance claims require actual measurements.

---

# 2. REQUIRED TECHNOLOGY STACK

| Component | Technology |
|---|---|
| Core application | C++20 |
| Download engine | libcurl multi interface |
| Desktop GUI | Qt 6 Widgets |
| Styling | Qt Style Sheets and custom Qt widgets |
| Database | SQLite |
| Browser extension | Chrome Manifest V3 / JavaScript |
| Browser communication | Chrome Native Messaging |
| Local IPC | Windows named pipes or another secure mechanism |
| Build system | CMake |
| Dependency manager | vcpkg |
| Compiler | MSVC |
| Target platform | Windows 10/11 x64 |
| Installer | Inno Setup |
| CI/CD | GitHub Actions |
| Version control | Git |
| Hosting | GitHub |

Do not replace the native application with Python, Electron, or a web application.

JavaScript is permitted for the Chrome extension and necessary build tooling.

Use modern C++20 practices, RAII, smart pointers, explicit ownership, and safe concurrency.

Use reproducible dependency versions.

Prefer mature libraries over custom implementations of complex networking or cryptographic protocols.

Avoid unnecessary dependencies, speculative abstractions, and overengineering.

---

# 3. CORE DOWNLOAD ENGINE

This is the highest-priority technical subsystem.

## 3.1 Networking Architecture

Use libcurl's multi interface for efficient concurrent HTTP/HTTPS transfers.

Implement:

- HTTP/1.1.
- HTTP/2.
- HTTP/3 when supported by the chosen libcurl build.
- TLS certificate verification.
- Connection reuse.
- Redirect handling.
- Connection and transfer timeouts.
- Retry policies.
- Exponential backoff with jitter.
- Per-host connection limits.
- Configurable bandwidth limits.
- Accurate speed measurement.
- Network error classification.

Prefer event-driven networking instead of creating one operating-system thread per connection.

Do not perform blocking network operations on the Qt GUI thread.

Do not disable TLS verification.

## 3.2 Server Capability Detection

Before starting a transfer:

1. Validate the URL and supported protocol.
2. Resolve redirects.
3. Inspect response headers.
4. Determine content length where possible.
5. Parse Content-Disposition safely.
6. Detect advertised byte-range support.
7. Verify range support using an actual partial-content request when necessary.
8. Validate HTTP 206 and Content-Range.
9. Determine whether segmentation is safe.
10. Select segmented or streaming mode.

Do not rely exclusively on Accept-Ranges.

Handle:

- Missing Content-Length.
- Unknown file size.
- Chunked responses.
- HTTP 200 responses to Range requests.
- HTTP 206 partial responses.
- HTTP 416 responses.
- Redirects.
- Expiring URLs.
- Server throttling.
- Authentication failures.
- Network interruptions.
- Servers that do not support Range requests.

Automatically fall back to a single streaming connection when segmented downloading is unsupported or unsafe.

Prevent HTTP content transformations from corrupting byte-range assembly.

## 3.3 Adaptive Segmented Downloading

Implement a dynamic segmented download scheduler.

Supported connection configurations:

- 1 connection.
- 4 connections.
- 8 connections.
- 16 connections.
- Automatic mode.

Automatic mode is the default.

Connection selection should consider:

- File size.
- Range support.
- Current throughput.
- Per-connection performance.
- Connection setup overhead.
- Server behavior.
- Resource utilization.
- Download stability.

Do not permanently divide a file into four equal segments.

Instead, maintain a dynamic queue of smaller byte ranges.

Workers must request new ranges as they finish existing assignments.

Faster workers should be able to complete more ranges.

Ensure that no two workers write overlapping ranges.

Validate each range response before accepting its data.

Use configurable chunk sizing with sensible defaults.

Avoid excessive connections that cause throttling or reduce performance.

## 3.4 Efficient Disk I/O

Implement safe, high-throughput file writing.

Requirements:

- Preallocate destination files when appropriate.
- Write chunks directly to correct offsets.
- Avoid unnecessary memory copying.
- Avoid buffering entire downloads in RAM.
- Prevent overlapping writes.
- Support files larger than 4 GB.
- Track completed byte ranges.
- Detect missing or incomplete segments.
- Validate expected file size.
- Support checksum verification when available.
- Use temporary partial files.
- Finalize completed downloads safely.

Handle:

- Insufficient disk space.
- Permission errors.
- Invalid filenames.
- Existing destination files.
- Interrupted writes.
- File-system errors.
- Unexpected process termination.

Never mark a file complete until all required ranges are verified.

## 3.5 Pause, Resume, and Crash Recovery

Implement reliable pause and resume.

Persist:

- Download ID.
- Source URL or secure reference.
- Destination path.
- Expected file size.
- Completed ranges.
- ETag.
- Last-Modified.
- Download status.
- Relevant timestamps.
- Resume capability.

Validate the remote resource before resuming.

Use conditional requests and remote file validators when available.

Never combine data from different versions of a remote resource.

If safe resumption is impossible, explain the limitation and offer a restart.

Support recovery following:

- Application crash.
- Windows restart.
- Network disconnection.
- Temporary server failure.
- Connection timeout.

Do not persist sensitive cookies, authorization headers, or tokens in plaintext.

## 3.6 Retries and Failure Handling

Implement bounded retries with exponential backoff and jitter.

Respect Retry-After where appropriate.

Distinguish transient failures from permanent errors.

Avoid endlessly retrying invalid URLs or authorization failures.

Provide meaningful error messages.

Maintain useful diagnostic logs without exposing secrets.

---

# 4. DOWNLOAD QUEUE AND MANAGEMENT

Implement a persistent download-management subsystem.

Required operations:

- Add download.
- Start.
- Pause.
- Resume.
- Cancel.
- Retry.
- Remove from history.
- Delete downloaded file with confirmation.
- Open downloaded file.
- Open containing folder.
- Copy source URL.
- Change destination filename.
- Select download directory.
- Change priority.
- Configure bandwidth limits.
- Configure connection count.
- Configure maximum simultaneous downloads.

Use SQLite for download history, queue state, settings, and recovery metadata.

Database operations must be safe under concurrent access.

Avoid blocking the GUI with database work.

Support configurable global and per-host concurrency limits.

---

# 5. MODERN WINDOWS GUI

Build the desktop interface using Qt 6 Widgets.

The application should look like a polished Windows 11 utility rather than a legacy download manager.

## 5.1 Design Requirements

Use:

- Light theme by default.
- White and soft neutral surfaces.
- Modern accent colors.
- Rounded controls.
- Clean typography.
- Spacious layouts.
- Consistent spacing.
- Clear visual hierarchy.
- Modern icons.
- Subtle shadows where appropriate.
- Lightweight progress animations.

Provide optional dark mode.

Avoid cluttered layouts, excessive gradients, unnecessary animations, and generic dashboard styling.

## 5.2 Main Window

Display:

- Active downloads.
- Queued downloads.
- Completed downloads.
- Failed downloads.
- Filename.
- Total size.
- Downloaded bytes.
- Progress percentage.
- Current speed.
- Estimated remaining time.
- Connection count.
- Download status.

Provide:

- Add Download button.
- Start/Pause controls.
- Search and filters.
- Download history.
- Settings.
- Aggregate download speed.

## 5.3 Download Details

Selecting a download should show:

- Source URL.
- Destination.
- Total size.
- Downloaded bytes.
- Current speed.
- Average speed.
- Elapsed time.
- Estimated remaining time.
- Active connections.
- Segment progress.
- Resume availability.
- Error details.

Use thread-safe communication between the engine and UI.

Limit unnecessary UI updates.

The interface must remain responsive during large downloads.

## 5.4 Settings

Include:

- Default download directory.
- Maximum simultaneous downloads.
- Connection count.
- Automatic connection optimization.
- Speed limits.
- Light/dark theme.
- Startup preferences.
- Notifications.
- Chrome integration status.
- Logging preferences.

---

# 6. CHROME EXTENSION — MANIFEST V3

Develop a complete Chrome extension that integrates with the native application.

## 6.1 Required Functionality

The extension must:

1. Detect eligible browser downloads.
2. Identify the download URL.
3. Determine whether interception is possible.
4. Communicate with the native application.
5. Transfer required download metadata.
6. Coordinate safe browser-to-native handoff.
7. Avoid duplicate downloads.
8. Support interception enable/disable.
9. Allow downloads to remain in Chrome.
10. Handle native application unavailability gracefully.

## 6.2 Native Messaging

Use Chrome Native Messaging.

Implement:

- Native Messaging host.
- Host manifest.
- Windows registration.
- Extension origin restrictions.
- Safe JSON message parsing.
- Message size validation.
- Connection lifecycle management.
- Error handling.
- Secure communication with the running GUI process.

Do not assume that the Native Messaging host and GUI are the same process.

Use secure local IPC where necessary.

## 6.3 Interception State Machine

Account for Chrome limitations:

- onCreated does not guarantee interception before data transfer begins.
- Browser cancellation and native acceptance are not atomic.
- Manifest V3 service workers can be suspended.
- Some downloads use expiring URLs.
- Some authentication contexts cannot be transferred.
- Some downloads cannot be reproduced outside the browser.

Implement a reliable handoff state machine.

Do not cancel a browser download until the native application has acknowledged that it can accept the request, subject to Chrome API limitations.

Avoid duplicate downloads and silent download loss.

If native handoff fails, preserve or restart the Chrome download where possible.

Do not claim universal interception.

## 6.4 Authentication Handling

Where authorized and technically supported, handle:

- Cookies.
- User-Agent.
- Referrer.
- Required request headers.
- Temporary download URLs.

Request only necessary Chrome permissions.

Do not collect unrelated browsing data.

Do not log credentials or sensitive headers.

Do not bypass browser security boundaries.

## 6.5 Extension Popup

Include:

- Native application connection status.
- Enable/disable interception.
- Open Download Manager.
- Basic settings.
- Error feedback.

Match the desktop application's visual identity.

---

# 7. SECURITY REQUIREMENTS

Security is mandatory for public distribution.

Implement:

- URL scheme validation.
- TLS certificate verification.
- Filename sanitization.
- Path traversal prevention.
- Secure destination handling.
- Restricted Native Messaging origins.
- Secure local IPC.
- Safe handling of authentication metadata.
- Resource limits.
- Bounded retries.
- Safe logging.
- Dependency security awareness.

Do not automatically execute downloaded files.

Do not implement DRM bypassing, paywall circumvention, or unauthorized access mechanisms.

Do not silently transmit user information to external services.

---

# 8. PERFORMANCE BENCHMARKING

Performance benchmarking is a required deliverable.

## 8.1 Benchmark Suite

Create a reproducible benchmark suite measuring:

- Total download time.
- Average throughput.
- Peak throughput.
- CPU utilization.
- Memory utilization.
- Disk write throughput.
- Connection overhead.
- Retry behavior.
- Recovery performance.

Test file sizes:

- 100 MB.
- 1 GB.
- 5 GB.

Test connection configurations:

- 1.
- 4.
- 8.
- 16.
- Automatic.

Include a controlled local HTTP test server with configurable Range support and network limitations.

Test both segmented and non-segmented downloads.

## 8.2 IDM Comparison

Where IDM is available, compare both applications using:

- Identical source URLs.
- Identical files.
- Identical hardware.
- Comparable network conditions.
- Repeated measurements.
- Median throughput.

Do not fabricate IDM results.

If IDM is unavailable, report independent benchmarks and clearly state that no direct comparison was performed.

## 8.3 Optimization

Investigate measured bottlenecks involving:

- Connection management.
- Segment scheduling.
- Disk I/O.
- Memory copying.
- Lock contention.
- HTTP protocol behavior.
- Excessive connections.
- GUI refresh overhead.

Optimize based on measurements, not assumptions.

---

# 9. PROJECT STRUCTURE

Use this structure as the initial guideline:

```text
download-manager/
├── CMakeLists.txt
├── CMakePresets.json
├── vcpkg.json
├── README.md
├── LICENSE
├── .gitignore
├── .github/
│   └── workflows/
│       ├── build.yml
│       └── release.yml
├── src/
│   ├── main.cpp
│   ├── core/
│   │   ├── DownloadEngine/
│   │   ├── SegmentScheduler/
│   │   ├── ConnectionManager/
│   │   ├── FileWriter/
│   │   ├── ResumeManager/
│   │   └── DownloadQueue/
│   ├── gui/
│   ├── database/
│   ├── browser/
│   └── utils/
├── extension/
│   ├── manifest.json
│   ├── background.js
│   ├── popup.html
│   ├── popup.js
│   └── icons/
├── tests/
├── benchmarks/
├── docs/
│   ├── ARCHITECTURE.md
│   ├── DEVELOPMENT_PROGRESS.md
│   ├── BENCHMARKS.md
│   └── INSTALLATION.md
├── assets/
└── packaging/
```

Adjust only when technically justified.

Keep modules cohesive and maintainable.

---

# 10. COMPLETE DEVELOPMENT ROADMAP

There are exactly 10 primary development phases.

Each phase must be completed, tested, documented, committed, and pushed before requesting approval for the next phase.

### Phase 1 — Native C++ Foundation

- Inspect the development environment.
- Configure MSVC, CMake, and vcpkg.
- Integrate Qt 6, libcurl, and SQLite.
- Establish the project structure.
- Build a minimal native Qt application.
- Verify successful compilation.
- Document setup instructions.

**Acceptance:** Reproducible Windows build and functioning application skeleton.

### Phase 2 — Segmented Download Engine

- Implement HTTP/HTTPS transfers.
- Implement server capability detection.
- Implement Range verification.
- Implement segmented downloading.
- Implement safe file writing.
- Test file integrity and fallback behavior.

**Acceptance:** Correct downloads with verified file integrity on both Range-supported and non-Range servers.

### Phase 3 — Adaptive Performance Engine

- Implement dynamic chunk scheduling.
- Implement configurable connection counts.
- Implement automatic connection optimization.
- Implement speed tracking.
- Benchmark connection configurations.

**Acceptance:** Correct adaptive scheduling with measured throughput and no overlapping or missing segments.

### Phase 4 — Resume and Recovery

- Implement pause/resume.
- Implement persistent range metadata.
- Implement crash recovery.
- Implement retries.
- Implement remote resource validation.
- Test interrupted downloads.

**Acceptance:** Downloads safely resume after supported interruptions without corrupting data.

### Phase 5 — Queue and Persistence

- Implement SQLite history.
- Implement persistent download queue.
- Implement priorities.
- Implement simultaneous download management.
- Implement settings persistence.

**Acceptance:** Queue and history survive application restarts and manage multiple downloads reliably.

### Phase 6 — Modern Desktop GUI

- Implement complete Qt desktop interface.
- Implement download list and details.
- Implement progress indicators.
- Implement settings.
- Implement light/dark themes.
- Verify UI responsiveness.

**Acceptance:** Functional, responsive desktop GUI connected to the real download engine.

### Phase 7 — Chrome Integration

- Implement Manifest V3 extension.
- Implement Native Messaging.
- Implement secure IPC.
- Implement download interception.
- Implement fallback behavior.
- Test supported Chrome downloads.

**Acceptance:** Supported downloads transfer successfully from Chrome to the native application, with tested failure handling.

### Phase 8 — Performance Optimization

- Run reproducible benchmarks.
- Identify bottlenecks.
- Optimize measured weaknesses.
- Test different file sizes.
- Test different connection counts.
- Document performance.
- Compare against IDM if available.

**Acceptance:** Verified benchmark results, documented optimization decisions, and no regressions in correctness.

### Phase 9 — Windows Packaging

- Build standalone Windows executable.
- Bundle required runtime dependencies.
- Create installer or portable distribution.
- Register Native Messaging host.
- Document extension installation.
- Test installation and uninstallation.

**Acceptance:** Application runs on a suitable Windows system without requiring developer tooling.

### Phase 10 — GitHub Release

- Finalize README.
- Finalize architecture documentation.
- Include screenshots where available.
- Include verified benchmarks.
- Configure GitHub Actions.
- Verify CI.
- Prepare release binaries.
- Publish verified release artifacts when authorized and technically possible.

**Acceptance:** Public repository, verified source, documentation, and release artifacts.

---

# 11. MANDATORY PHASE-GATED EXECUTION POLICY

**THIS SECTION OVERRIDES ANY CONFLICTING INSTRUCTION ELSEWHERE IN THE PROMPT.**

Implement exactly one approved phase at a time.

Never begin the next phase automatically.

### Mandatory Workflow

For every phase:

**IMPLEMENT → BUILD → TEST → FIX → DOCUMENT → COMMIT → PUSH → REPORT → STOP**

### Rules

1. Work independently within the approved phase.
2. Do not request approval for individual files, functions, or minor decisions.
3. Complete the phase's required implementation.
4. Run relevant builds and tests.
5. Fix defects before declaring completion.
6. Run regression tests for previously completed functionality.
7. Update README and DEVELOPMENT_PROGRESS.md.
8. Create a descriptive Git commit.
9. Push the completed phase to GitHub.
10. Verify the push succeeded.
11. Provide a concise completion report.
12. **STOP and wait for explicit approval.**

Do not start the next phase even if sufficient time, tokens, or context remain.

Do not interpret silence or previous approval as authorization to continue.

An approval for Phase N authorizes Phase N only.

### Significant Architectural Changes

If a change would substantially alter previously approved architecture, dependencies, data formats, or security boundaries:

- Explain the reason.
- Describe the impact.
- Request approval before making the change.

Minor implementation decisions within the approved architecture do not require approval.

---

# 12. AUTOMATIC GIT AND GITHUB WORKFLOW

GitHub integration is mandatory.

The user will provide access to the intended repository.

## 12.1 Repository Setup

At the beginning:

1. Inspect the existing repository.
2. Preserve existing history.
3. Initialize Git only if necessary.
4. Configure `.gitignore`.
5. Verify GitHub authentication.
6. Verify the remote URL.
7. Use the authorized repository.
8. Create a dedicated development branch if appropriate.

If no repository exists, create one only when authorized and technically possible.

Never overwrite unrelated repositories.

Never assume authentication succeeded.

## 12.2 Phase Commits

Use descriptive commit messages:

```text
feat(phase-01): initialize native C++ foundation
feat(phase-02): implement segmented download engine
feat(phase-03): implement adaptive download scheduling
feat(phase-04): add pause resume and recovery
feat(phase-05): implement queue and persistence
feat(phase-06): build modern Qt desktop interface
feat(phase-07): integrate Chrome download interception
perf(phase-08): benchmark and optimize engine
build(phase-09): package Windows application
docs(phase-10): finalize GitHub release
```

Commits must represent actual work.

Never create empty commits solely to satisfy the roadmap.

Never commit secrets, credentials, cookies, private keys, build directories, or unnecessary generated binaries.

## 12.3 Automatic GitHub Push

After completing a verified phase:

1. Stage relevant files.
2. Create the phase commit.
3. Verify the commit exists.
4. Push to the authorized GitHub remote.
5. Confirm the push succeeded.
6. Record the commit hash.
7. Update the phase report.
8. Stop for approval.

Do not force-push or rewrite unrelated history.

If branch protection prevents a direct push, use a suitable development branch and pull request workflow.

A phase is not considered fully published until its intended remote push succeeds.

## 12.4 Incomplete Phase Checkpoints

If work is interrupted before phase completion:

- Preserve source files.
- Record outstanding work.
- Record known failures.
- Update DEVELOPMENT_PROGRESS.md.
- Commit recoverable work with a clearly identified WIP commit when possible.
- Push to the development branch when possible.
- Do not mark the phase complete.
- Do not advance to the next phase.

WIP commits must be clearly distinguishable from completed phase commits.

Do not include secrets or broken generated artifacts.

## 12.5 GitHub Actions

Configure workflows for:

- Windows CMake builds.
- Dependency installation.
- Unit tests.
- Integration tests where feasible.
- Packaging verification.
- Release artifact generation.

Do not claim CI passed without a successful run.

## 12.6 Final Release

During Phase 10:

- Verify completed phase commits.
- Verify GitHub pushes.
- Verify CI results.
- Prepare release notes.
- Create a versioned release.
- Publish verified installer and portable binaries when permissions allow.
- Include Chrome extension installation instructions.

Do not publish unverified binaries.

---

# 13. CODEX USAGE LIMITS AND SESSION RECOVERY

The project is expected to span multiple Codex sessions and possibly multiple usage-limit windows.

Design the development workflow accordingly.

## 13.1 Persistent Progress Tracking

Maintain:

`docs/DEVELOPMENT_PROGRESS.md`

Record:

- Current phase.
- Last completed phase.
- Last approved phase.
- Completed tasks.
- Remaining tasks.
- Important architectural decisions.
- Build status.
- Test results.
- Known issues.
- Latest commit hash.
- GitHub push status.
- Next required action.

Update this file during each phase and at every checkpoint.

## 13.2 Recovery After Interruption

If execution stops due to usage limits, context limits, environment failure, or session interruption:

1. Preserve completed work.
2. Record the exact interruption point.
3. Commit and push recoverable work when possible.
4. Do not claim incomplete work is finished.
5. Do not advance to the next phase.

When the user starts a new Codex session:

1. Inspect the repository.
2. Read DEVELOPMENT_PROGRESS.md.
3. Inspect Git history and working-tree status.
4. Identify the last completed and approved phase.
5. Determine whether the current phase is incomplete.
6. Resume that phase from the last valid checkpoint.
7. Avoid regenerating completed code.
8. Preserve established architecture.
9. Continue until the current phase is verified.
10. Commit, push, report, and stop for approval.

If the previous phase is already complete and pushed, wait for approval rather than starting another phase.

## 13.3 Token Efficiency

Minimize unnecessary conversational output.

Do not repeatedly restate requirements.

Do not produce lengthy explanations during implementation.

Prefer concise status reporting.

Use repository files as the persistent source of truth.

Do not sacrifice correctness, testing, or security merely to reduce token usage.

---

# 14. TESTING AND ACCEPTANCE CRITERIA

Test the application progressively.

### Networking Tests

- HTTP downloads.
- HTTPS downloads.
- Redirects.
- Range-supported servers.
- Servers ignoring Range requests.
- Unknown content length.
- HTTP errors.
- Timeouts.
- Connection interruptions.
- Retry behavior.

### File Integrity Tests

- Correct file sizes.
- Correct byte ordering.
- No missing segments.
- No overlapping writes.
- Large-file support.
- Checksum validation where available.
- Safe file finalization.

### Recovery Tests

- Pause/resume.
- Application restart.
- Unexpected process termination.
- Network disconnection.
- Changed remote files.
- Expired URLs.

### Browser Integration Tests

- Successful interception.
- Native application unavailable.
- Native Messaging failure.
- Service-worker suspension.
- Duplicate prevention.
- Authentication limitations.
- Browser fallback.

### GUI Tests

- Responsive updates.
- Multiple downloads.
- Queue operations.
- Settings persistence.
- Window resizing.
- Theme switching.
- Error handling.

### Performance Tests

- Connection-count comparison.
- Throughput.
- CPU utilization.
- Memory utilization.
- Large-file behavior.
- Resource stability.

Never fabricate test results.

Clearly distinguish passed, failed, and unexecuted tests.

A phase must satisfy its acceptance criteria before being marked complete.

---

# 15. DOCUMENTATION REQUIREMENTS

Maintain the following documentation.

### README.md

Include:

- Project overview.
- Features.
- Screenshots when available.
- Technology stack.
- Installation.
- Usage.
- Chrome integration.
- Build instructions.
- Benchmark methodology.
- Known limitations.
- License.

### ARCHITECTURE.md

Document:

- Download engine.
- Segment scheduling.
- Connection management.
- File writing.
- Resume metadata.
- SQLite design.
- Browser integration.
- Security boundaries.

### BENCHMARKS.md

Document:

- Test environment.
- Hardware.
- Network conditions.
- File sizes.
- Connection counts.
- Measured throughput.
- CPU and memory measurements.
- IDM comparisons where performed.
- Limitations.

### DEVELOPMENT_PROGRESS.md

Document:

- Current phase.
- Completed phases.
- Approval status.
- Test results.
- Commit hashes.
- Push status.
- Known issues.
- Next actions.

Keep Markdown whitespace-clean.

Avoid trailing whitespace and unnecessary blank lines.

---

# 16. OPEN-SOURCE LICENSING

The application will be publicly released for free.

Select an appropriate open-source license after verifying compatibility with Qt, libcurl, SQLite, and other dependencies.

Prefer a permissive license where legally and technically compatible.

Do not include proprietary IDM code, artwork, trademarks, or copied implementation details.

Use original branding and implementation.

Include required third-party notices.

Make the project reproducibly buildable.

---

# 17. FINAL DELIVERABLES

The completed project must contain:

1. C++20 source code.
2. libcurl-based download engine.
3. Adaptive segmented scheduler.
4. Pause/resume and crash recovery.
5. SQLite queue and history.
6. Modern Qt desktop interface.
7. Chrome Manifest V3 extension.
8. Secure Native Messaging integration.
9. Automated tests.
10. Performance benchmark suite.
11. Windows executable.
12. Installer or portable distribution.
13. GitHub Actions workflows.
14. Complete documentation.
15. GitHub repository with phase-by-phase commits.
16. Verified release artifacts.

End users must not need Python, CMake, Qt development tools, or a compiler to run the distributed application.

---

# 18. PHASE COMPLETION REPORT

After each phase, provide a concise report containing:

**Phase:** Number and title.

**Status:** Completed, blocked, or incomplete.

**Implemented:** Summary of actual functionality.

**Build:** Result and environment.

**Tests:** Executed tests and results.

**Git Commit:** Actual commit hash.

**GitHub Push:** Verified status and branch.

**Known Issues:** Any remaining limitations.

**Next Phase:** Brief description only.

Finish with:

**"Phase X completed and pushed to GitHub. Awaiting your approval to begin Phase X+1."**

Use this statement only when the phase is genuinely complete and the push succeeded.

If incomplete, state the exact blocker or remaining work instead.

Do not begin the next phase.

---

# 19. FIRST EXECUTION INSTRUCTION

Begin with **Phase 1 — Native C++ Foundation** only.

Perform the following:

1. Inspect the development environment.
2. Verify the available Windows compiler and build tools.
3. Configure CMake and vcpkg.
4. Integrate Qt 6, libcurl, and SQLite.
5. Establish the initial project architecture.
6. Create the minimal native Qt application.
7. Verify successful compilation and startup where execution is available.
8. Run relevant initial tests.
9. Create and update documentation.
10. Commit the verified Phase 1 implementation.
11. Push the commit to the authorized GitHub repository.
12. Provide the Phase 1 completion report.
13. **STOP and wait for explicit approval before Phase 2.**

Do not implement Phase 2 during this execution.

For every later phase, follow the same mandatory cycle:

**IMPLEMENT → BUILD → TEST → FIX → DOCUMENT → COMMIT → PUSH → REPORT → WAIT FOR APPROVAL**

If the current phase is interrupted, resume it from the last valid Git checkpoint when the user returns.

Never restart the entire project unnecessarily.

Never automatically advance between phases.

The user controls phase progression. You control engineering implementation within the approved phase.

**START PHASE 1 NOW.**
