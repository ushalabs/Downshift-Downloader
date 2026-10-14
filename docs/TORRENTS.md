# Torrent engine: Phase T1

T1 is the native engine foundation. Torrent management/persistence (T2), Qt torrent pages/dialogs (T3), and full torrent packaging acceptance (T4) require separate approval. The existing HTTP GUI/browser remain on libcurl and SQLite. T1 registers no torrent file association or magnet browser takeover.

## Dependency and architecture

The immutable vcpkg baseline resolves Rasterbar libtorrent 2.1.2. Default features are disabled: native C++, OpenSSL, no Python binding, deprecated-function ABI or WebTorrent/WebRTC. The port exports ABI 100 and OpenSSL/libcrypto definitions through LibtorrentRasterbar::torrent-rasterbar; Downshift consumes that imported target rather than defining ABI macros. Existing dependencies remain on the same baseline. This is Rasterbar, not the unrelated rTorrent library.

The independent downshift_torrent QObject adapter owns a command worker and native session. Parsing, filesystem work and alerts run off the Qt thread. set_alert_notify only wakes the worker; pop_alerts pointers never survive the next pop or enter Qt signals. Status is requested at most twice per second; queued Qt connections receive copied snapshots. Shutdown disables notification, aborts the session and joins network/disk workers before releasing synchronization objects. Await asynchronous shutdown outside the GUI thread before destruction; the destructor joins as a lifetime safeguard.

Upstream references: [session/alert lifetime](https://libtorrent.org/reference-Session.html), [native metadata loading](https://libtorrent.org/reference-Core.html), [loading limits](https://libtorrent.org/reference-Torrent_Info.html).

## T1 scope

- Native .torrent and magnet parsing, v1/v2/hybrid hashes, duplicate detection and asynchronous add results.
- Separate metadata-loading state and cancellation. Unavailable peers do not make a valid magnet invalid.
- Native tracker/DHT/PEX discovery, piece selection/verification and peer transfers, with no HTTP segment scheduler.
- File-list and byte/rate/status snapshots for later Qt integration.
- Automatic pause at verified completion. Uploads can occur during downloading; no indefinite post-download seeding policy is implemented in T1.

Production enables DHT/LSD and the default metadata/PEX extensions. Automatic UPnP/NAT-PMP mapping is disabled. Listening uses available ports; outgoing transfers can work without port forwarding. Tests disable discovery and use loopback peers; they do not establish public tracker/DHT reachability.

## Developer usage

The new DownshiftTorrent.exe console target is beside the development binaries:

```powershell
./build/windows-msvc/Release/DownshiftTorrent.exe --torrent 'D:/metadata/example.torrent' --save 'D:/Downloads/Torrents/example'
./build/windows-msvc/Release/DownshiftTorrent.exe --magnet 'magnet:?xt=urn:btih:VALID_HASH' --save 'D:/Downloads/Torrents/example' --timeout 600
```

Use an actual valid magnet hash. Output reports state, verified bytes, total bytes, payload rate and peer count without peer addresses/source URLs. Exit 0 means verified completion, 1 error, 2 invalid invocation, 3 bounded wait expired. Timeout does not imply invalid metadata. This tool is T1's acceptance surface; torrents are not yet accessible in the GUI.

## Input and resource limits

Local metadata is bounded to 10,000,000 bytes before read/parse; magnets to 32,768 characters. Native decode/piece limits apply, with at most 10,000 files and eight entries per engine instance and a session-wide 64-peer ceiling. The per-torrent upload-limit setting is 256 KiB/s; native peer-class rules still apply. These are foundation defaults, not user-configurable T2 controls or an independently benchmarked bandwidth guarantee.

After libtorrent sanitization, Windows paths reject relative destinations, traversal, device names, unsafe characters, collisions, symlinks and existing junctions. Magnet payloads remain in upload mode until metadata passes validation. Embedded web seeds, rename overrides and file selections are ignored in T1; all ordinary files download from peers. Nothing executes automatically. Cancel/shutdown never delete data. These checks are not an OS sandbox against another process changing destinations concurrently.

## Recovery, privacy and licensing

T1 does not save resume data or torrent SQLite rows. Exit retains files but cannot automatically restore sessions. Re-adding metadata invokes native verification; unverified pieces are never complete. File priorities, persisted pause/resume, crash recovery, speed settings, seeding policies and detailed tracker/peer management are T2; sidebar/history/dialogs are T3.

Ordinary BitTorrent exposes your IP to peers and trackers. Downshift promises no anonymity and adds no telemetry. Avoid publishing private tracker tokens. Tests generate deterministic bytes, without copyrighted media or unauthorized sources.

Rasterbar uses BSD-3-Clause with embedded component notices; the pinned port also records Zlib/BSL/APSL/BSD notices. OpenSSL uses Apache-2.0 and Boost BSL-1.0. Preserve exact installed-port notices and sources when packaging; see THIRD_PARTY_NOTICES.md. No T1 release publication is authorized.

## Verification status

The normal MSVC Release and Debug builds passed all 17 local test groups. Each torrent group passed 142 assertions, including six complete local-peer transfers across piece boundaries, all twelve final-file SHA-256 comparisons, runtime/header version match (2.1.2.0, ABI 100), invalid/oversized input, symlink/file-count rejection, duplicates, unavailable metadata/cancellation, destination errors, queued Qt delivery and startup/shutdown races. Release CLI help and invalid invocation exit behavior also passed. Complete HTTP, encrypted queue/recovery, browser and GUI regressions remain included. See DEVELOPMENT_PROGRESS.md and evidence/torrent-t1-local.json for final results and scopes. Public swarm/DHT/tracker reachability, multi-peer load, disk-full and full torrent recovery/seeding policy acceptance remain unverified/deferred; this is T1, not T4.
