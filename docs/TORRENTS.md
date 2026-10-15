# Torrent engine, management and Qt interface: Phases T1–T3

T1 supplies the native foundation and T2 management/persistence. The current direct request authorizes T3 Qt integration after passing T2. T4 full acceptance and packaging require subsequent approval. The existing HTTP GUI/browser remain on libcurl and SQLite. No torrent file association or magnet browser takeover is registered.

## Dependency and architecture

The immutable vcpkg baseline resolves Rasterbar libtorrent 2.1.2. Default features are disabled: native C++, OpenSSL, no Python binding, deprecated-function ABI or WebTorrent/WebRTC. The port exports ABI 100 and OpenSSL/libcrypto definitions through LibtorrentRasterbar::torrent-rasterbar; Downshift consumes that imported target rather than defining ABI macros. Existing dependencies remain on the same baseline. This is Rasterbar, not the unrelated rTorrent library.

The independent downshift_torrent QObject adapter owns a command worker and native session. Parsing, filesystem work and alerts run off the Qt thread. set_alert_notify only wakes the worker; pop_alerts pointers never survive the next pop or enter Qt signals. Worker status polling runs at most twice per second, including idle seeds so time policies advance without peers; queued Qt connections receive copied snapshots. Shutdown disables notification, aborts the session and joins network/disk workers before releasing synchronization objects. Await asynchronous shutdown outside the GUI thread before destruction; the destructor joins as a lifetime safeguard.

Upstream references: [session/alert lifetime](https://libtorrent.org/reference-Session.html), [native metadata loading](https://libtorrent.org/reference-Core.html), [loading limits](https://libtorrent.org/reference-Torrent_Info.html).

## T1 scope

- Native .torrent and magnet parsing, v1/v2/hybrid hashes, duplicate detection and asynchronous add results.
- Separate metadata-loading state and cancellation. Unavailable peers do not make a valid magnet invalid.
- Native tracker/DHT/PEX discovery, piece selection/verification and peer transfers, with no HTTP segment scheduler.
- File-list and byte/rate/status snapshots for later Qt integration.
- T1 automatically paused at verified completion. T2 adds explicit completion seeding policies below; uploads can also occur while downloading.

Production enables DHT/LSD and the default metadata/PEX extensions. Automatic UPnP/NAT-PMP mapping is disabled. Listening uses available ports; outgoing transfers can work without port forwarding. Tests disable discovery, use TCP loopback peers, allow multiple fixture peers on the same IP and lower only fixture reconnect backoff; they do not establish public tracker/DHT reachability.

## Developer usage (T1-compatible ephemeral mode)

The new DownshiftTorrent.exe console target is beside the development binaries:

```powershell
./build/windows-msvc/Release/DownshiftTorrent.exe --torrent 'D:/metadata/example.torrent' --save 'D:/Downloads/Torrents/example'
./build/windows-msvc/Release/DownshiftTorrent.exe --magnet 'magnet:?xt=urn:btih:VALID_HASH' --save 'D:/Downloads/Torrents/example' --timeout 600
```

Use an actual valid magnet hash. Output reports state, verified bytes, total bytes, payload rate and peer count without peer addresses/source URLs. Exit 0 means verified completion, 1 error, 2 invalid invocation, 3 bounded wait expired. Timeout does not imply invalid metadata. This tool is T1's acceptance surface; torrents are not yet accessible in the GUI.

## Input and resource limits

Local metadata is bounded to 10,000,000 bytes before read/parse; magnets to 32,768 characters. Native decode/piece limits apply, with at most 10,000 files and eight entries per engine instance and a session-wide 64-peer ceiling. The per-torrent upload-limit setting is 256 KiB/s; native peer-class rules still apply. These are foundation defaults, not user-configurable T2 controls or an independently benchmarked bandwidth guarantee.

After libtorrent sanitization, Windows paths reject relative destinations, traversal, device names, unsafe characters, collisions, symlinks and existing junctions. Magnet payloads remain in upload mode until metadata passes validation. Embedded web seeds, rename overrides and file selections are ignored in T1; all ordinary files download from peers. Nothing executes automatically. Cancel/shutdown never delete data. These checks are not an OS sandbox against another process changing destinations concurrently.

## T1 historical scope, privacy and licensing

The original T1 foundation did not save resume data or torrent SQLite rows. Its ephemeral mode retains files but cannot automatically restore sessions. Persistent T2 usage below supersedes that limitation. Re-adding metadata invokes native verification; unverified pieces are never complete. Sidebar/shared-history/dialogs remain T3.

Ordinary BitTorrent exposes your IP to peers and trackers. Downshift promises no anonymity and adds no telemetry. Avoid publishing private tracker tokens. Tests generate deterministic bytes, without copyrighted media or unauthorized sources.

Rasterbar uses BSD-3-Clause with embedded component notices; the pinned port also records Zlib/BSL/APSL/BSD notices. OpenSSL uses Apache-2.0 and Boost BSL-1.0. Preserve exact installed-port notices and sources when packaging; see THIRD_PARTY_NOTICES.md. No T1 release publication is authorized.

## T2 management and persistence

T2 extends the independent native torrent target and developer console. HTTP transfer, queue, GUI, media and extension source remain unchanged. Torrent widgets, shared-history rows, associations and magnet browser takeover are T3.

TorrentEngine supports add-paused, pause/resume, checkpoints, file selection/priorities, per-torrent and session limits, seeding policies, copied tracker/peer/availability details, cancellation, remove-only and confirmed delete-data. Commands, SQLite and native queries run on its worker; await futures outside the GUI thread. Up to eight retained torrents use resolved, nonoverlapping destination folders below filesystem roots. Remove retained records before re-adding their info hash. Failed disk transfers can be explicitly resumed after fixing the cause; unsafe metadata stays blocked.

### Storage boundary

An absolute Options.databasePath or console --database enables persistence; empty means an ephemeral session. The intended T3 integration is a companion torrents.sqlite under the verified user data directory, beside queue.sqlite. The normal application does not open it during T2. The torrent subsystem rejects queue.sqlite, renamed unrelated SQLite files and stores inside payload destinations. It never migrates the HTTP schema or opens its queue connection.

Schema version 1 (user_version=1, application ID DSTR) uses torrents, torrent_settings and torrent_meta tables. User-scoped Windows DPAPI authenticates/encrypts complete versioned records, metadata/native resume data, destinations, tracker tokens, priorities, counters, seeding rules and settings. In SQLite, only opaque IDs and schema remain plaintext. This does not protect against software running as the same Windows user. Prepared statements, WAL, FULL synchronization and secure deletion apply; record/input bounds remain enforced.

A path lease and Windows volume/FileID mutex prevent competing owners, including hard-link aliases. The managed .owned marker binds the physical database identity and rejects a replacement behind the same displayed path. Missing/truncated owned live storage, future versions, invalid records, wrong hashes and DPAPI tampering fail visibly without accepting empty history. SQLite backup and QSaveFile provide an encrypted .backup.sqlite after complete validation and clean shutdown. The live database is never automatically overwritten from a backup. Deliberate restoration requires verified content and rebinding the new physical identity; no automatic repair command is provided.

An encrypted acceptance marker changes atomically with Add/Remove. Unexpected disappearance of every torrent row fails even when the database remains structurally valid; it cannot rotate over a valid backup. Explicit removal of the last torrent records an intentional empty state that is allowed after restart. Missing/tampered acceptance metadata also fails visibly.

### Recovery and selected-file correctness

Add first commits recoverable native metadata/magnet state. For metadata-backed torrents, explicit flush_cache/cache_flushed_alert completes before save_resume_data(save_info_dict) serializes checkpoints. The pinned implementation queues flush_disk_cache without waiting before posting resume data; that flag alone is insufficient. Metadata-free magnets save directly because no payload storage exists. Explicit pause/configuration/selection/checkpoint futures settle after encryption and SQLite commit. Overlapping saves are serialized/coalesced; later commands wait for a later native save. Periodic checkpoints run every 15 seconds. Shutdown pauses handles, drains saves/removals with a bounded 20-second alert wait, then joins native workers. Failure reports an error while retaining previous checkpoints.

Selection changes wait for the native file-priority disk operation before checkpointing. Newly selected files are rehashed after part-file migration; their earlier in-memory piece map cannot establish correctness of moved bytes. Both Complete and Seeding transitions wait for the native flush/checkpoint, and an earlier seeding snapshot is invalidated when selection changes. The repeated selection regression fails against the previous backend and passes with this ordering repair.

Restart and process-death recovery always restore paused. Explicit Resume rehashes actual files before reuse/seeding, even after same-size/same-mtime content changes. Libtorrent owns piece validity; SQLite does not invent a completed-piece map. Uncheckpointed written pieces may be rediscovered by rechecking, but recovery of those bytes is not promised. Missing peers/metadata remain waiting states.

Priorities correspond to ordinary Snapshot.files: 0 skips; 1–7 set native priority. At least one file must be selected; internal pad-file indices are handled separately. Selection can change after metadata. Magnet input selections/web seeds/rename overrides remain ignored unless explicit validated options are supplied. Boundary pieces can include skipped-file bytes stored in the native part file. Progress sums verified selected-file bytes rather than whole-piece wanted counters. Selected completion requires every selected file and a completed native flush/checkpoint; skipped files are not reported complete.

### Limits, statistics and seeding

Download/upload limits use bytes/second (0 unlimited); connection limits are 1–512. Session budgets affect torrents only. Defaults remain 64 session peers, eight retained torrents and 256 KiB/s per-torrent upload. IP peer classes include LAN/loopback in the global budget. Native pacing can briefly burst; controlled sustained loopback checks are separate from Internet throughput guarantees. Only loopback test mode reduces reconnect backoff to one second; production retains native reconnect behavior.

Copied snapshots include selected/total/verified bytes, rates, seeds/peers, lifetime payload counters, availability and upload/download ratio. Explicit details queries copy file progress, tracker verification/failure summaries, peer addresses/clients/rates and piece availability. Default console progress omits addresses/source URLs. --details explicitly prints them and may reveal private tracker tokens; do not publish that output.

Seeding modes are stop (default), ratio, time and manual. Ratio uses native lifetime payload uploaded/downloaded; with zero downloaded bytes it remains zero. Time counts active post-selected-completion seeding, persists milliseconds and excludes pauses. Half-second policy polling continues for idle seeds and can slightly overshoot thresholds. Manual runs until pause/cancel/shutdown. Uploads during downloading remain possible in every mode. Restored sessions never upload automatically.

Cancel retains history and files. Remove-only erases the record and keeps payload. Delete-data requires explicit confirmation, validated metadata and fresh link/path checks; its future settles after native deletion acknowledgement and SQLite removal. Unrelated files remain. Cancelled known-metadata records attach a paused native storage handle for confirmed deletion. Interrupted removal stays cancelled with a visible message and never silently repeats deletion. This is not a sandbox against concurrent filesystem changes by another process.

### Persistent developer console

Use actual valid inputs and keep the store outside payload destinations:

```powershell
./build/windows-msvc/Release/DownshiftTorrent.exe --database 'D:/DownshiftData/torrents.sqlite' --torrent 'D:/metadata/example.torrent' --save 'D:/Downloads/Torrents/example' --paused --priorities '7,0'
./build/windows-msvc/Release/DownshiftTorrent.exe --database 'D:/DownshiftData/torrents.sqlite' --list
./build/windows-msvc/Release/DownshiftTorrent.exe --database 'D:/DownshiftData/torrents.sqlite' --id 'SAVED_ID' --resume --timeout 600
./build/windows-msvc/Release/DownshiftTorrent.exe --database 'D:/DownshiftData/torrents.sqlite' --id 'SAVED_ID' --configure --download-limit 1048576 --upload-limit 262144 --connections 32 --seed time --seed-seconds 3600
./build/windows-msvc/Release/DownshiftTorrent.exe --database 'D:/DownshiftData/torrents.sqlite' --id 'SAVED_ID' --remove
```

Additional ID actions are --pause, --cancel, --details and --delete-data --confirm-delete. --global-limits applies the rate/connection options session-wide. Each invocation restores entries paused; only explicit Resume/add-start transfers. Seeding runs continue until policy completion or bounded timeout. Clean timeout preserves checkpoints. Exit codes remain 0 success/verified selected completion, 1 error, 2 invalid action combination, 3 timeout.

Upstream references: [resume serialization and trust](https://libtorrent.org/reference-Resume_Data.html), [pause, save alerts, priorities and native limits](https://libtorrent.org/reference-Torrent_Handle.html), [session peer classes](https://libtorrent.org/reference-Session.html).

### T2 verification

The final T2 explicit-flush repair passes 315 management and 142 foundation checks per local build. Earlier 313-check results and both failed CI runs remain historical. Full regression coverage, initial local seeder timeouts, the first CI selected-file hash failure and subsequent repair are recorded in DEVELOPMENT_PROGRESS.md and evidence/torrent-t2-*. Exact-source Windows CI delivery is recorded separately after the main push. Fixtures use private stores/destinations and hidden process children. Public swarms, disk-full, multi-peer stress and packaged torrent acceptance remain T4. No new release is published; T3 is not started automatically.

## T1 historical verification status

Exact-source Windows CI run 38570946997 passed 17/17 Release groups, 17/17 Debug groups and 13 existing HTTP/GUI portable checks, without skipped test groups. See evidence/torrent-t1-ci.json. This does not constitute a public torrent release or future torrent GUI/persistence acceptance.

The normal MSVC Release and Debug builds passed all 17 local test groups. Each torrent group passed 142 assertions, including six complete local-peer transfers across piece boundaries, all twelve final-file SHA-256 comparisons, runtime/header version match (2.1.2.0, ABI 100), invalid/oversized input, symlink/file-count rejection, duplicates, unavailable metadata/cancellation, destination errors, queued Qt delivery and startup/shutdown races. Release CLI help and invalid invocation exit behavior also passed. Complete HTTP, encrypted queue/recovery, browser and GUI regressions remain included. See DEVELOPMENT_PROGRESS.md and evidence/torrent-t1-local.json for final results and scopes. Public swarm/DHT/tracker reachability, multi-peer load, disk-full and full torrent recovery/seeding policy acceptance remain unverified/deferred; this is T1, not T4.

## T3 Qt integration

Choose Torrents in the sidebar, then Add torrent. Paste a magnet link or browse for a .torrent file, choose a separate destination and Read metadata. Parsing and peer lookup run on the native torrent worker. The preview does not request payload pieces; magnets pause after metadata arrives. Choose file priorities (Skip or 1–7) and confirm Add paused, or explicitly enable Start downloading. Cancel removes the preview record while retaining any existing files. Unknown peers/metadata can remain waiting; cancel is available. A preview interrupted by app shutdown restores paused and remains subject to file confirmation.

All downloads shows typed HTTP/video/torrent rows together; filters, search and sorting use stable identifiers. Torrents has its own filter. Resume, Pause and Cancel route to the selected row's native adapter. Seeding, verified selected progress, upload rate, seeds, peers and ratio remain torrent-specific. Details opens Overview, Files, Trackers, Peers and Settings. Apply files and settings commits native selection, per-task limits and stop/ratio/time/manual seeding policy. Torrent limits in the sidebar controls the aggregate native session, not HTTP scheduling. Tracker hosts are displayed without URL tokens; peer addresses are visible only in the explicit details window.

Normal startup uses a companion torrents.sqlite in the already resolved HTTP data directory. The HTTP database/schema/settings are not migrated or copied into it. Qt merges copied snapshots for display only. Optional encrypted torrent-record fields add creation time and metadata-only preview status within schema version 1; older T2 records default to non-preview. Startup restores paused and rechecks before payload reuse. Main-window close keeps the event loop responsive while checkpointing; the worker retains native ownership and joins at teardown.

No .torrent association, magnet protocol registration, browser magnet interception or automatic file execution is added. The existing 0.4.4 browser/media code stays in place. New commits and pushes are reserved for the user. T3 is locally verified: all 21 groups pass per configuration across the full batches and affected GUI reruns, including 67 GUI checks in Release and 67 in Debug. Exact full-run failures and corrected fixture reruns are distinguished in evidence/torrent-t3-local.json. No new CI, package or release is claimed. Public-swarm/load/disk-full and packaged acceptance remain T4.
