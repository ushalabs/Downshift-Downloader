# Downshift — complete new-chat handoff

## Current continuation update: Torrent T2 (2026-10-14)

The user directly authorized Torrent T2 in the subsequent chat and explicitly excluded changes to the existing HTTP direct downloader. T2 management/persistence now extends the separate native torrent target and developer console: DPAPI-encrypted companion SQLite storage, native checkpoints, paused recovery with rehash, selection/priorities, independent limits, seeding policies, statistics and confirmed removal/deletion. HTTP/media/GUI/browser source remains unchanged; the normal application does not yet expose torrent controls. Consult the newest DEVELOPMENT_PROGRESS.md entry and torrent-t2 evidence for actual validation/commit/push status. T3 Qt torrent integration requires explicit approval; do not automatically start it or publish a release. The older T1 handoff below is historical context.

Copy this document into the new chat. This is a factual project handoff as of 14 October 2026, followed by instructions for continuing. Inspect the actual checkout before relying on any recorded status. Do not restart the project or automatically begin an unapproved phase.

## Role and objective

Act as a senior Windows C++20 systems engineer, Qt desktop developer, network-performance engineer and build engineer. Continue **Downshift**, a native download manager for ordinary files, supported videos and, eventually, integrated BitTorrent. Preserve the working application and the user's real history, settings and partial downloads.

The product should provide dependable browser handoff, high throughput where the source/network permit, correct completion, durable recovery and a compact modern pink interface. Reliability and integrity take priority over throughput. IDM is a functional/performance reference; no universal IDM parity has been established. Do not copy proprietary implementation or substitute URL-specific patches for general engineering.

## Workspace, repository and versions

- Work only in `C:\Users\HP\Desktop\Projects\Downshift`. The older Codex working directory under Documents is not the product checkout.
- Repository: https://github.com/ushalabs/Downshift-Download-Manager
- Branch: `main`. Git identity previously supplied: `ushalabs`, `ushakurosaki@gmail.com`.
- Current working HTTP application and Chrome extension: **0.4.2**. Native torrent foundation T1 is added to this source version.
- Latest verified published portable preview: **v0.3.3**. It is older than current source. No 0.4.x or torrent release has been published.
- T1 implementation: `69c0310a6edfbf9d479831ecc5a7e0db0a2a2f31`, `feat(torrent-t1): integrate libtorrent engine`, pushed to main and verified against the remote.
- History repair: `abac103bb4f2fc0b597956b7af9a190460979335`.
- Independent-connection performance repair: `21912f37ca8097e72d5de9a5720fc44907499bc6`.
- Subsequent documentation commits record delivery, CI and this handoff. Inspect `git status`, `git log` and remote state for the actual latest HEAD; do not reset to an implementation commit.
- Original requirements, GUI 2.0 and torrent briefs are preserved in project docs and local attachments. The latest applicable direct instruction/phase gate wins over older historical restrictions.

## First actions in the new chat

1. Read `AGENTS.md`, the newest top entries of `docs/DEVELOPMENT_PROGRESS.md`, this handoff, `docs/HISTORY_RECOVERY.md`, `docs/TORRENTS.md` and relevant source. Some lower progress sections retain old Phase 8/manual-Git instructions; those are historical.
2. Check Git changes before editing. Preserve unrelated work and private ignored data.
3. Establish the actual running app, storage binding and current history before touching persistence. Do not launch a test queue and present it as the user's app.
4. Report the current phase concisely. **T1 is completed. T2 requires explicit user approval.** This handoff alone does not authorize T2, T3, T4 or another release. If the user says “Start T2,” execute T2 autonomously and stop afterward.

## Technology and architecture already built

Native Windows 10/11 x64, C++20, MSVC v143, CMake, vcpkg, Qt 6 Widgets, libcurl multi, SQLite, Windows DPAPI, Chrome Manifest V3/JavaScript, Native Messaging and secure Windows named pipes. The torrent subsystem uses **Rasterbar libtorrent**, not the unrelated rTorrent library.

The immutable vcpkg baseline is `e456309491fd875487bf02b8a0dca76c1b1b00cc`, with shared `x64-windows` libraries and matching MSVC runtime. Current dependency preparation verified Qt 6.11.2, SQLite 3.53.4, Rasterbar port 2.1.2/runtime and headers 2.1.2.0/ABI 100, Boost 1.92.0 and OpenSSL 3.6.5. Inspect the installed port inventories for exact revisions. Curl uses Windows TLS verification and HTTP/2; HTTP/3 is not selected.

Core ownership:

- The queue service owns SQLite, serialized commands and scheduling off the GUI thread. Each active HTTP job has an engine worker; that engine uses libcurl multi for its ranges.
- Resume journals, verified range hashes and remote revalidation govern reusable bytes. SQLite owns history/queue state, not an alternative piece/range authority.
- Native Messaging is a separate restricted host communicating with the desktop through secure per-user IPC.
- Media helpers run separately from native HTTP scheduling and report persistent media records.
- TorrentEngine is a separate QObject adapter/worker/native session. It does not replace curl or add HTTP-style scheduling to torrent pieces.

No Electron or web-app replacement. Python/Node serve tests/build tooling, not the native HTTP or torrent engine. Existing optional video extraction helpers are a separate established subsystem.

## Built: general HTTP/HTTPS downloading

- Ordinary direct HTTP/HTTPS files, including programs, archives, audio and video files; Downshift is not only a video downloader.
- 64-bit file sizes, byte counters, offsets and ranges. There is no deliberate 2/4 GB download ceiling. A 50–100 GB file can be supported subject to disk space, filesystem/server capabilities and resumability; a full 50–100 GB acceptance transfer has not been performed.
- Real range probing and exact range/length geometry checks. A real first segment is validated before parallel fanout. Strong validators protect segmented resume and source version consistency.
- Adaptive segmented transfers and fixed 1–16 connections, bounded retries/backoff, lazy nonoverlapping ranges and size-aware startup.
- Independent physical TCP connections for active HTTP/2 ranges, with bounded reusable keepalive connections. The 0.4.1 repair disables curl multiplexing through `CURLPIPE_NOTHING`; HTTP/2 remains available.
- Generic full-response streaming fallback when safe ranges/validators are unavailable or refused. Safe content decoding is supported. Changed remote versions and invalid framing still fail rather than producing corrupt files.
- Verified TLS certificates/hosts, bounded redirects/timeouts, no HTTPS downgrade and no plaintext URL credentials or silent netrc credential use.
- Sanitized suggested filenames, Content-Disposition handling and rejection of unexpected HTML/page content for expected file downloads.
- Exclusive partial files, encrypted durable resume journals, ownership checks, flushed range checkpoints, local rehashing and remote validation before resume.
- Pause, resume, cancel and explicit safe restart. Cancel preserves partials; restart archives old partial state. Nonresumable streaming transfers cannot promise partial resume. Previously uncheckpointed bytes cannot be promised back.
- Transient retries, exponential backoff/jitter, Retry-After, cancellation and deadlines.
- Optional trusted SHA-256; size/hash/flush checks and atomic final naming without overwriting an existing file. Completed means a verified final file exists.
- Receive/write/hash buffering and performance diagnostics for CPU, memory, writes, hashing and checkpoint time.

## Built: queue, history, settings and file locations

- Persistent SQLite HTTP/media queue and history, encrypted settings and durable browser handoff receipts. User-scoped DPAPI encrypts complete record payloads, including sensitive URLs/query tokens/destinations. This is not protection against other software running as the same Windows user.
- Single-owner database lease, prepared statements, WAL and FULL synchronization. Damaged/unavailable history or failed durable acceptance fails visibly rather than silently replacing state.
- Priorities, simultaneous/per-origin download limits, connection reservations and global/per-file pacing. Media concurrency/rate budgeting remains separate.
- Add, start/resume, pause, cancel, retry/restart, unstarted filename changes, history removal and explicitly confirmed completed-file deletion. Downloaded programs never execute automatically.
- Interrupted HTTP transfers restore as paused; startup queue behavior respects saved settings. Interrupted media needs a video retry rather than claiming native byte-range resume.
- Search and sidebar filtering. **Latest first** is the default; **Latest, Oldest and Size** sort the records relevant to each sidebar page. The redundant status dropdown was replaced by sorting.
- Default category destinations below the configured Downloads base folder: `Videos`, `Audios`, `Zip` for archives, `Programs` for other files. Users can choose another destination for every download and change the base folder. Classification uses filename/MIME; it is not full-content decoding.
- Consistent encrypted SQLite backups after successful startup, explicit Pause and clean shutdown, with ownership tracking and rotation of eight managed backups.
- Loading, unavailable history, retry and genuine empty states are distinct. An encrypted intentional-empty marker allows deliberate removal of the last entry; unexpected empty storage with owned nonempty backups is rejected.

### Critical history incident and repair — do not undo

The user repeatedly paused a large download, reopened through the **Desktop shortcut**, and saw zero history. Earlier development-tool reopen tests had passed, but did not exercise the same physical storage view.

The captured 0.4.1 normal Explorer-launched app opened a 24 KiB database while a tool open behind the same displayed LocalAppData path saw a 36 KiB database holding six encrypted records. Windows file identities differed on the same volume; neither file was delete-pending. This confirmed two physical stores behind an apparently identical path. **The exact filesystem redirection component remains unidentified.** Do not invent a database-deletion/corruption explanation or blame a particular component without new evidence.

0.4.2 adds optional `downshift-storage.json` beside the EXE:

- Version 1, absolute `dataDirectory`, optional `expectedIdentity` of 32 lowercase hex characters.
- User storage is beneath `<dataDirectory>/users/<Windows SID>/queue.sqlite`.
- An identity derived from Windows volume/FileID is checked before opening/creating the database. A mismatch fails visibly; it does not create a replacement empty queue.
- Without this binding, normal storage remains Windows LocalAppData under `Usha Labs/Downshift`.

**This local development installation** has private ignored Release/Debug bindings to:

`C:\Users\HP\Desktop\Projects\Downshift\.downshift-data`

Keep that directory, binding files, backups and partials intact. Do not publish SIDs, fingerprints, signed URLs or raw private forensic archives. Do not casually move/replace the database, since its physical identity changes. Migration must use consistent SQLite backup and a deliberately verified binding update, never an unexamined live overwrite.

Both previous store branches were archived. Six encrypted records/preferences were migrated, retaining the user's intentional removal of one failed entry. Actual Explorer reopening showed six records and the paused download at 79%. During T1 the user added a seventh record; rebuilding/reopening retained **seven records, one paused, zero active**, unchanged encrypted downloads/settings/receipts digest and matched storage identity. Counts are observations, not permanent expectations: inspect current state and preserve later user changes. Do not restore removed entries or resume the user's download as a test.

## Built: Chrome integration

- MV3 extension at `C:\Users\HP\Desktop\Projects\Downshift\extension`.
- Stable extension ID: `empnpdkfnffpcgnfgdndkbielplcoioe`.
- Normal Native Messaging host: `org.ushalabs.downshift`; executable `DownshiftNativeHost.exe`; per-user HKCU Chrome registration and restricted extension origin.
- Content scripts, popup, media discovery, capture toggle, native launch/focus and destination confirmation.
- Supported direct file anchors are intercepted before Chrome's downloader. A downloads-API fallback promptly pauses Chrome, prepares a durable native handoff, then cancels/erases the owned browser entry only after accepted transfer. Receipt/commit/cancel/reconciliation prevent duplicate native downloads.
- Capture defaults on for fresh installs; saved off preference persists. Enabled failed handoffs remain paused with visible retry/error instead of silently continuing in Chrome. Turning capture off restores Chrome downloading.
- Known file links, dynamic page/video controls, observed media requests, video-player overlays and YouTube SPA watch/shorts URL normalization. Invalid/feed/recommended URLs are rejected rather than downloaded as HTML videos.
- Browser tests use separate `.test` host/pipe, private Chrome profiles and hidden fixture queues. They do not replace the user's normal registration.

Setup for this development checkout: keep the complete Release runtime directory, register the host through `scripts/Register-ChromeHost.ps1` according to its current parameters/docs, load `extension` unpacked at `chrome://extensions`, and reload the extension AND already-open pages after changes. Never assume a content script hot-reloads with edited source.

Limits: Chrome may receive initial bytes or finish a very small fallback download before notification. Cancellation notifications are not universally suppressible. Authentication cookies/credentials are not imported. Browser-only blob/POST, login-only, incognito, dangerous and protected/DRM workflows are not universally supported. A button on a detected player does not establish downloadability of every video. Do not claim Chrome's entire downloader has been replaced in all cases. Some real handoffs can still fail; diagnose actual generic capture/native/network stages before claiming repair.

## Built: video extraction and actual quality choices

- Optional checksum-verified pinned upstream **yt-dlp, Deno, FFmpeg/ffprobe** installed beside the app. These helper executables are not redistributed in the portable ZIP.
- Native media dialog extracts available metadata/formats and displays actual supported quality choices, lets the user select quality/destination, downloads/muxes video and audio and verifies the final container/streams/file.
- Media records persist in history; errors/retry folders survive. Completed requires an actual verified local video filename; saving YouTube watch HTML cannot count as video completion.
- Historical 0.3.2 live YouTube full download succeeded, with independently verified video/audio/container/hash. A later live 0.3.3 source refused with 429/sign-in restrictions; that was recorded as refusal, not a successful download. Current controlled quality/mux/verification fixtures pass.

YouTube/site behavior changes. Live access is not guaranteed. No DRM bypass, cookie harvesting or universal site support. Do not remove this established media subsystem merely because the torrent brief prohibits a Python torrent implementation.

## Built: desktop GUI and branding

- Qt 6 Widgets GUI 2.0, pink arrow/ring icon embedded in Windows resources and Qt without relying on optional image plugins.
- Light neutral and dark charcoal themes with pink actions/progress/selection; shared rounded controls, dialogs, menus, dropdowns, scrollbars and tooltips.
- Default **1000×650 logical pixels**, minimum **800×550**, compact navigation below 950, responsive maximization and accessible actions/search.
- Sidebar **All downloads, In progress, Completed, Needs attention**, plus Settings. No Torrents page yet.
- Summary cards, search, per-page sorting, painted file/status/progress rows, stable ID-based selection, resume/pause/cancel/more actions and contextual collapsible details.
- Destination/source, speed/ETA/connections, errors and diagnostics; clear loading/empty/no-match/failure/retry states.
- General, Downloads, Appearance, Integration and Advanced settings; preference-preserving validation and Light/Dark preview.
- Native Windows caption controls remain. GUI screenshot assets contain synthetic fixtures, not real downloads or measured internet performance.
- Completion notifications use Windows tray support where available and a status-bar message. Closing the window exits and cooperatively pauses workers; there is no persistent hidden downloader process after closing. Basic rotating activity logs omit source URLs/server messages.

## Built: packaging, CI and GitHub releases

Original phases 1–8, Phase 9 portable packaging and Phase 10 GitHub publication were completed, followed by compatibility, media/capture, GUI, transport and history repairs. Follow the current documents/evidence rather than obsolete lower Phase 8 gates.

| Original phase | Delivered scope |
|---|---|
| 1 | Native C++ foundation/build/dependency setup |
| 2 | Segmented HTTP/HTTPS engine |
| 3 | Adaptive performance engine |
| 4 | Verified resume and recovery |
| 5 | Queue and encrypted persistence |
| 6 | Native desktop GUI, later modernized through GUI 2.0 |
| 7 | Chrome extension/native handoff |
| 8 | Measured optimization and benchmark evidence |
| 9 | Portable Windows packaging route; a signed installer remains absent |
| 10 | Verified GitHub releases through v0.3.3 |

- Windows x64 portable folder/ZIP with required release DLLs, Qt Windows plugin and app-local Microsoft VC143 runtime.
- `Setup.cmd`, optional `Enable-video.cmd`, ownership-safe per-user setup/uninstall, Desktop shortcut and Native Messaging registration.
- Clean-source packaging, manifests/hashes, extension archive, application source and corresponding third-party source/patch/license bundles; Qt library replacement supported.
- GitHub Actions source build/tests/package artifacts and separate release publication controls.
- Published immutable previews through **v0.3.3**; current source builds are newer.

The Desktop shortcut launches:

`C:\Users\HP\Desktop\Projects\Downshift\build\windows-msvc\Release\Downshift.exe`

It is a shortcut, not a standalone EXE copied without dependencies. Retain all DLLs/plugins. Before relinking a running EXE, inspect activity, gracefully close only the idle normal app, preserve data, rebuild and reopen/verify. An EXE lock can cause LNK1104; do not force-kill active user work.

**Not built/published:** signed Windows installer (including the original requested Inno Setup route), MSI/MSIX/store distribution, code signing, automatic updater, Chrome Web Store listing, fresh clean-VM end-to-end certification, public 0.4.x/torrent release. Existing 13 portable checks validate the HTTP/GUI package, not future torrent GUI/resume acceptance.

Other absent scope: FTP/SFTP engine, selected HTTP/3 transport, Linux/macOS versions, authenticated-browser credential transfer, universal website/video support and a proven universal IDM feature/speed match. Do not present these as existing capabilities.

## Performance: what is established and what is not

The user observed IDM around 3 MB/s while Downshift was substantially slower. Eight reported ranges did not necessarily mean eight physical TCP connections. Pre-repair curl HTTP/2 multiplexing shared one TCP connection; 0.4.1 now uses independent reusable connections for active ranges.

Evidence:

- Controlled real-TLS HTTP/2 server with a per-socket 512 KiB/s cap: eight ranges/one TCP approximately 0.50 MiB/s before, eight/eight approximately 3.63 MiB/s after.
- Modeled shared-link background traffic: approximately 0.40 versus 1.30 MiB/s in that fixture.
- Bounded same saved URL partial observation: last smoothed rate 472,130 versus 2,446,178 B/s, one versus eight TCP connections. This was not a complete independently hashed internet transfer or simultaneous IDM benchmark.
- Four 32 MiB samples ordered 8,16,16,8: eight connections 3.61/2.94 decimal MB/s, sixteen 2.86/3.47. Medians 3.28 versus 3.16. No consistent advantage from sixteen; no user settings were changed based on that result.
- Earlier Phase 8 retained 180 verified loopback samples/90 pairs/30 cases over 100 MB, 1 GB and 5 GB payloads, including regressions. These are cached local benchmarks, not internet/physical-disk guarantees.

Remaining questions: conservative per-origin reservation can allocate eight per job with sixteen site slots/two admitted jobs even when another reserved job is idle. Hashing/checkpoint flushes before range refill may affect throughput; causal impact is not established. Server/CDN endpoint and connection/session throttles matter. Identical filenames alone do not establish identical endpoints. Measure exact source, limits, independent TCP count, disk/checkpoint cost and background load before tuning.

No router/system traffic-priority mechanism is installed. Downshift cannot guarantee an ISP speed or make all other network activities slow. Do not remove TLS/hash/version validation or blindly raise connection limits to claim IDM parity.

## Completed: Torrent Phase T1 only

Independent native Rasterbar engine and **developer acceptance CLI** are built. `DownshiftTorrent.exe` is an in-repo native development target, not an external torrent client and not the finished GUI feature.

Implemented:

- Native `.torrent` and magnet parsing; v1/v2/hybrid hashes, duplicate checks, asynchronous add results.
- Native metadata retrieval/loading, copied file-list/size snapshots and cancellation. No peers means metadata remains loading; it is not automatically an invalid magnet.
- Peer transfers with native libtorrent piece verification/scheduling, trackers/DHT/PEX/LSD configured through the library. UPnP/NAT-PMP automatic mapping is off. Tests use loopback/discovery-off; public discovery success is unverified.
- QObject pimpl, worker-owned session, asynchronous commands and copied queued Qt snapshots. Alert pointers never cross into Qt or survive the next pop. Updates at most twice per second; safe callback shutdown/session abort and joined native workers.
- Bounded metadata/magnet parsing, destination/Windows-name/traversal/collision/symlink/junction checks. Magnet payload remains held until validated metadata. Embedded web seeds, renames and file selections are ignored in T1; all ordinary files download from peers.
- Foundation limits: 10,000,000 metadata bytes, 32,768 magnet characters, 10,000 files, eight entries and session-wide 64 peers; per-torrent upload setting 256 KiB/s subject to native peer-class semantics. These are not completed configurable T2 controls or proven universal rate guarantees.
- Completion after verified pieces automatically pauses; uploads can occur during download, but indefinite post-completion seeding is not enabled. Cancel/shutdown preserve data.
- No HTTP scheduler/database changes. No torrent file associations, browser magnet takeover or GUI page.

Developer usage from the project root:

```powershell
./build/windows-msvc/Release/DownshiftTorrent.exe --torrent 'D:/metadata/example.torrent' --save 'D:/Downloads/Torrents/example'
./build/windows-msvc/Release/DownshiftTorrent.exe --magnet 'ACTUAL_VALID_MAGNET_URI' --save 'D:/Downloads/Torrents/example' --timeout 600
```

Exit codes: 0 verified completion, 1 error, 2 invalid invocation, 3 bounded timeout. Output omits source URLs/peer addresses. T1 saves no torrent resume data/SQLite rows: exiting retains files, but re-adding requires native verification and does not automatically restore the session. Ordinary BitTorrent exposes the user's IP; no anonymity or telemetry claims.

## Not built: Torrent T2, T3 and T4

**T2 — management and persistence:** user pause/resume, native resume-data serialization and safe persistence, versioned SQLite integration preserving HTTP records/preferences/receipts, restart/crash recovery, selected files and priorities, multiple-torrent management, user-facing global/per-torrent download/upload and connection limits, seeding ratio/time/manual policies, tracker/peer/availability/ratio/error statistics, confirmed remove/delete-data. Native engine rows/basic cancellation are foundation support, not completed T2 management.

**T3 — unified Qt GUI:** Torrents sidebar alongside existing pages, HTTP/torrent type indicator and shared history integration, Add Torrent/magnet dialog, asynchronous metadata/file selection, start/add-paused options, Overview/Files/Trackers/Peers details, upload/seeds/peers/ratio displays and torrent settings/seeding controls. Integrate with the current design system; do not perform another full GUI rewrite. File association/magnet command routing must be explicitly configured and verified if added.

**T4 — full acceptance, optimization and packaging:** complete multi-torrent/multi-peer resource/load tests, selected-file behavior, network interruption/no peers/tracker failure, disk-full/permission errors, invalid/tampered resume data, process death/recovery, removal/delete confirmation, seeding limits, HTTP regression and current packaged torrent runtime/license/source audit. Fresh public-swarm, GUI and recovery acceptance remain unverified. Do not confuse earlier portable checks with this phase.

Per-phase workflow: **IMPLEMENT → BUILD → TEST → FIX → DOCUMENT → COMMIT → PUSH → REPORT → STOP**. Do one authorized phase. Work autonomously within it; ask only for missing material information/major changes. Do not ask again for routine previously authorized engineering/Git steps. Verify pushes, never force-push. Required phase titles:

- `feat(torrent-t2): add torrent management and persistence`
- `feat(torrent-t3): integrate torrents into Qt interface`
- `test(torrent-t4): verify and package torrent support`

At completion report implemented scope, build/tests, commit, verified branch push, limitations and next phase; stop for explicit next approval. If interrupted, preserve work and record honest incomplete status in progress docs, with a labeled checkpoint when appropriate.

## Verified current results and evidence

- T1 local MSVC Release: **17/17 groups**, 336.34 seconds. Debug: **17/17**, 397.74 seconds; no skipped groups.
- Each torrent group: **142 assertions**, six complete deterministic local-peer transfers (v1/v2/hybrid, each file and magnet) and twelve independent final-file SHA-256 comparisons. Multi-file regular payload 1,048,795 bytes per transfer crosses piece boundaries; tests also cover invalid/corrupt/oversized input, duplicates, path/file-count rejection, unavailable metadata/cancel, Qt delivery and startup/shutdown races.
- Separate actual isolated Chrome tests: **15 checks in each Release and Debug**.
- T1 exact-source Windows CI: https://github.com/ushalabs/Downshift-Download-Manager/actions/runs/38570946997 — **success**, source `69c0310...`, 17 Release groups/210.77 seconds, 17 Debug/216.86 seconds, zero skipped groups, **13 existing portable checks**.
- History repair exact-source CI: run 38554413240 — **success**, 16 Release/16 Debug/13 portable. Its local Debug suite initially had one GUI timing timeout under overlap; the isolated retry passed. Do not describe that first local attempt as clean.
- Evidence: `docs/evidence/torrent-t1-{local,ci,delivery,chrome-release,chrome-debug}.json`, `history-042-*`, `transport-*`, `gui2-*`, earlier release/capture/media evidence.
- User history preservation was verified separately from hidden fixture queues. Local, remote CI, live internet and release publication are different acceptance scopes.

## Build and test on this Windows machine

Compiler/SDK are already installed: Visual Studio 2022 C++ tools, MSVC 14.44.35207 and Windows SDK 10.0.26100.0. Dependencies are cached/installed; do not reinstall or upgrade unnecessarily. Use PowerShell and keep Bootstrap/configure in the same session:

```powershell
Set-Location 'C:\Users\HP\Desktop\Projects\Downshift'
. ./scripts/Bootstrap.ps1
./scripts/Test-Environment.ps1
$env:VCPKG_MAX_CONCURRENCY = '2'
cmake --preset windows-msvc
cmake --build --preset release --parallel 2
./scripts/Install-MediaTools.ps1 -Configuration Release
ctest --preset release --output-on-failure
cmake --build --preset debug --parallel 2
./scripts/Install-MediaTools.ps1 -Configuration Debug
ctest --preset debug --output-on-failure
```

Run full Release/Debug and browser/native tests sequentially; concurrent suites can contend for fixture pipes/resources and create misleading GUI timeouts. Keep all validation queues/windows private and hidden. Do not stop real transfers merely to test.

Actual isolated Chrome runner:

```powershell
$env:DOWNSHIFT_NODE_MODULES = 'C:\Users\HP\.cache\codex-runtimes\codex-primary-runtime\dependencies\node\node_modules'
node tests/ChromeTests.cjs `
  '.tools/chrome-testing/chrome-win64/DownshiftBrowserTest.exe' `
  'build/windows-msvc/Release/downshift_browser_tests.exe' `
  'build/windows-msvc/Release/DownshiftNativeHost.exe' `
  'C:\Users\HP\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' `
  'build/new-chat-chrome-release'
```

The last argument is a unique **output directory**, not a report filename; it contains `report.json`. Use corresponding Debug binaries and a separate directory for Debug. Confirm runtime paths still exist before use.

Use `scripts/Build-Portable.ps1 -RequireClean` and `scripts/Test-Portable.ps1` for authorized packaging; inspect current parameters and generated filename instead of copying old versioned examples. Private `build/*Github*.ps1` helpers exist locally but are untracked/version-specific; do not assume them portable or run old mutation modes with obsolete hardcoded releases. Use secure Git/GitHub tooling; never print credentials/tokens. A prior working push command was `git -c http.sslBackend=openssl push origin main`; verify remote afterward.

## Source/document map

- `src/core/DownloadEngine/`: curl protocol/transfer logic.
- `src/core/ConnectionManager/`, `SegmentScheduler/`, `FileWriter/`, `ResumeManager/`: adaptation, retry/ranges, writes/hashes and encrypted recovery.
- `src/core/DownloadQueue/QueueManager.*`, `StorageLocation.*`, `Categories.h`, `VideoUrl.h`: scheduling/history, bound physical storage, destination categories and page normalization.
- `src/database/QueueStore.*`: encrypted SQLite records/settings/receipts/backups/migrations.
- `src/gui/`: MainWindow, dialogs, MediaDialog, design/brand/painted delegate.
- `src/browser/`: restricted host, protocol and pipe server.
- `src/torrent/TorrentEngine.*`, `src/TorrentCli.cpp`, `tests/TorrentTests.cpp`: T1 foundation and acceptance.
- `extension/`: MV3 manifest, background/handoff/media/file links/video button/popup.
- `tests/`: native tests and controlled Python/Node HTTP/TLS/queue/recovery/media/browser/GUI fixtures.
- `scripts/`: environment/bootstrap, media installation, host registration, packaging, publishing and benchmarks.
- `docs/ARCHITECTURE.md`, `QUEUE.md`, `RECOVERY.md`, `HISTORY_RECOVERY.md`, `CHROME.md`, `GENERAL_DOWNLOADS.md`, `MEDIA.md`, `GUI_2.md`, `PERFORMANCE.md`, `TORRENTS.md`, `INSTALLATION.md`, `PACKAGING.md`, `TESTING.md`, `THIRD_PARTY_NOTICES.md`, `REBUILD_LIBRARIES.md`, `DEVELOPMENT_PROGRESS.md`.

## Rules for continuing safely and honestly

- Extend the native application; preserve HTTP/media behavior while adding a separate torrent adapter. Never block the Qt GUI thread with networking, disk or SQLite work.
- Do not erase/recreate history to make a test pass, alter saved settings without a user action, overwrite partials/downloads or restore deliberately removed entries.
- Before database migrations, make a consistent encrypted backup, test upgrades privately and verify original IDs/preferences/receipts and paused recovery survive in the normal physical store.
- Never silently accept unverified ranges/pieces or announce completion without verified files. Keep TLS and dependency hash checks enabled.
- Never automatically execute a downloaded file. Destructive file deletion requires clear explicit confirmation.
- Do not expose private URLs, tracker tokens, download filenames, peer IPs, SIDs or credentials in public docs/logs/screenshots. No unnecessary telemetry.
- Preserve exact dependency/license/source obligations; app code is MIT, shared Qt uses the LGPLv3 option, Rasterbar and dependencies retain their own notices.
- Tests must use generated/free data and separate hosts/profiles/queues; user downloads are not fixture inputs unless explicitly authorized.
- Do not bulk-delete ignored build/private-data directories. There are archived forensic stores and user-related recovery artifacts. A prior automatic approval review rejected cleanup of an isolated test journal; leave it alone rather than retrying deletion through another route.
- Do not publish a newer release or change Windows/browser associations outside current approval. Public v0.3.3 remains immutable.
- Do not spawn delegated agents unless the user or applicable instructions explicitly requests delegation.
- Report actual evidence and failures, including test scope and remaining unknowns. A configured capability is not the same as a verified public-network outcome.

**Current endpoint: HTTP/Chrome/media/GUI/history repair and Torrent T1 are built and verified. Torrent T2–T4 and a new public release are pending. Await the user's explicit next-phase instruction.**
