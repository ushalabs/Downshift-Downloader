# Downshift architecture

## Torrent foundation and management (T1–T2)

The independent downshift_torrent target wraps native Rasterbar through its exported CMake/ABI target. An owned worker serializes parsing, commands, alerts and bounded status requests; Qt receives copied queued snapshots. HTTP classes, rate budgets, scheduler and SQLite payloads are not repurposed for peer/piece scheduling. T2 adds a worker-owned DPAPI-encrypted companion TorrentStore, native resume checkpoints, paused recovery with rehash, selection, limits and seeding policies. Its versioned SQLite tables, path/physical-store leases and backups never alter the HTTP queue. DownshiftTorrent exercises management while Qt torrent pages and shared-history integration remain T3. See TORRENTS.md.

## Current execution boundaries

The native Windows application uses C++20, MSVC v143, Qt 6 Widgets, libcurl multi with Schannel TLS/HTTP2, and SQLite's C API. Qt and the MSVC runtime are dynamically linked. CMake's x64 Visual Studio generator and a pinned vcpkg baseline build Debug and Release. HTTP/3 is not selected.

`src/main.cpp` owns `QApplication`, `CurlRuntime`, and `QueueManager`, validates linked dependencies, and opens `MainWindow`. The window submits asynchronous commands and polls bounded snapshots; the queue service owns SQLite and starts engine workers. Blocking networking/database/file operations stay outside the GUI thread. The Phase 2 console executable, `DownshiftDownload.exe`, owns its own `CurlRuntime` and calls the engine on its main worker thread. See [desktop](DESKTOP.md) and [queue](QUEUE.md).

## Implemented engine

| Component | Responsibility |
|---|---|
| `core/DownloadEngine/DownloadEngine.*` | Validated request, curl multi session, owned easy handles, callbacks, bounded polling, fallback, completion |
| `core/DownloadEngine/HttpResponse.*` | Separate response headers, strict lengths/ranges, strong validators, safe filename suggestions |
| `core/SegmentScheduler/SegmentScheduler.h` | Lazy, resizable, nonoverlapping 64-bit ranges |
| `core/ConnectionManager/AdaptiveController.h` | Size-based startup, throughput trials, rollback, resource-pressure backoff, bounded chunk sizing |
| `core/ConnectionManager/SpeedTracker.h` | Monotonic sampling, smoothed rate, bounded progress refresh, sampled peak |
| `core/FileWriter/FileWriter.*` | Exclusive partial handle, disk allocation, positional writes, CNG SHA-256, safe rename |
| `core/ResumeManager/ResumeJournal.*` | Exclusive task lease, bounded DPAPI ledger, durable checkpoints, archival restart |
| `core/ConnectionManager/RetryPolicy.h` | Bounded exponential/jitter delay and Retry-After floor |
| `src/DownloadCli.cpp` | Development download client, pause/resume controls, and secret-safe errors |

An actual `Range: bytes=0-0` GET probes support. `Accept-Ranges` is advisory only. Segmentation requires a valid `206`, exactly `0-0/total`, untransformed content, a one-byte body, and a strong quoted ETag. A missing/weak ETag causes a fresh complete streaming request. A server returning `200`, an empty representation returning `416 bytes */0`, or a malformed range probe also uses streaming. Other HTTP errors fail without a completed destination.

The scheduler assigns ranges once; completed handles receive the next range. Changing the chunk size changes only unassigned bytes. Faster connections can take more ranges while slower requests drain. Active/pending requests are bounded by the configured maximum, and the recovery ledger has a separate 100,000-range limit. Resumption lazily schedules gaps around saved ranges. Each segment sends `If-Match`, requires `206`, checks exact first/last/total, verifies the ETag, and enforces its byte count. A changed representation, incorrect range, segment redirect, missing metadata, or oversized body aborts. Incomplete transport bodies are eligible for bounded retries of their original interval. The engine counts verified bytes across saved and new ranges before finalization. `--connections 4 --chunk-kib 4096 --retries 0` retains the fixed Phase 2 regression baseline.

## Phase 3 tuning and measurements

Automatic mode is the default. It starts with one slot for files up to 1 MiB, two below 32 MiB, and four for larger files, bounded by the configured maximum (16). Fixed counts bypass tuning. The controller compares one-second occupied-pool payload windows, doubles the desired concurrency for a trial, waits one window for startup to settle, and keeps the increase only with at least 10% throughput gain. Otherwise it restores the previous target and stops exploring until observed capacity increases by 40%. Two windows below 75% of the reference rate halve the target. Write calls occupying over 65% of a sample interval also trigger backoff. Tail/draining pools do not train the controller. This is a bounded heuristic, not a guarantee of the globally optimal connection count.

Reducing concurrency never aborts an assigned range; the pool drains to the new target before assigning replacements. The configured curl connection limits remain hard upper bounds. A slot is a concurrent range request: HTTP/2 may multiplex several slots onto fewer physical connections. The loopback benchmark speaks HTTP/1.1 and records both slots and curl-reported new connections.

Adaptive chunks target roughly 0.5-2 seconds of per-slot data, using aggregate smoothed payload rate, completed-request rates when no live sample exists, and time to first byte. Chunks are quantized to 256 KiB and bounded at 16 MiB. The unassigned tail is balanced across the desired slots; a final range may be smaller than the minimum. Explicit `--chunk-kib` uses a fixed size. Existing byte boundaries remain immutable.

Speed sampling uses `steady_clock` and monotonic accepted-payload counters at 250 ms intervals. A two-second time-based exponential smoother supplies the displayed rate/ETA. Progress distinguishes written bytes from completed verified ranges and includes a stage for final checksum/flush. Fallback resets the current file's written/verified count; aggregate payload counters retain discarded writes. Counters exclude probe bytes, headers, and TLS/TCP framing. Observers run outside C callbacks on the owning worker; observers must copy snapshots for queued use and return promptly. An observer exception disables further notifications, is reported in result metrics, and does not damage the file.

Transfer timing includes local recovery validation, capability probing/allocation, retry delays, and per-range hash/flush checkpoints, and ends before finalization. Total engine timing includes finalization. Final average goodput uses newly completed bytes (excluding recovered bytes) divided by transfer time. The sampled peak uses positive windows of at least 250 ms; files finishing sooner use their shorter boundary sample. CPU time and peak working set are process metrics with an availability flag. Write timing/bytes measure cached Windows file calls, not physical disk bandwidth. Checksum, allocation, and flush timings are separate. Curl setup/first-byte timing follows [its timing contract](https://curl.se/libcurl/c/CURLINFO_STARTTRANSFER_TIME_T.html); [new connections](https://curl.se/libcurl/c/CURLINFO_NUM_CONNECTS.html) are counted per request. Setup durations may overlap and are not subtracted from wall time.

If a segment instead returns `200` with the same strong ETag, all active handles are detached and destroyed before truncating the partial. A conditional full GET then writes from byte zero. Streaming must return `200`, untransformed content, and no `Content-Range`; a known strong ETag must still match. Content-Length, chunked framing, and EOF-delimited responses are supported. Only an independently supplied checksum can establish the intended contents of an unknown-length response beyond transport completion.

Callbacks capture exceptions into `exception_ptr`; no C++ exception crosses libcurl's C boundary. Easy handles, header lists, URL objects, multi handles, file handles, and hash objects have explicit RAII ownership. `curl_multi_poll` waits at most 100 ms per iteration. A stop token provides cooperative worker cancellation and leaves a partial. A separate atomic DownloadControl provides persisted pause: active requests are detached, completed ranges are flushed/checkpointed, and the worker exits with Paused. A new worker resumes after local/remote validation. Network/disk callbacks occur on the owning worker; positional writes are serialized while connections progress concurrently.

Phase 8 requests a bounded 256 KiB libcurl receive buffer before each segmented handle starts. The pinned libcurl shares that receive buffer across a multi handle's transfers; this does not expand individual write callbacks. See [receive buffers](https://curl.se/libcurl/c/CURLOPT_BUFFERSIZE.html) and [callback bounds](https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html). Each active segmented response owns at most 64 KiB of pending body data. Validated callbacks are paced before buffering; contiguous bytes reach `FileWriter` in full blocks, and a validated successful response flushes its short tail. A failed/pending response discards its uncheckpointed tail. Streaming retains direct writes and default receive buffers. Retry progress subtracts written bytes, avoiding underflow when a response fails before filling a block. Download progress counts actual written bytes, so pending buffers can delay updates by less than 64 KiB per active response.

`FileWriter` reuses a 1 MiB heap buffer for independent file checksum reads. Range digests still reread disk/cache bytes, data still flushes before every recovery publication, and final trusted SHA-256 still rereads the entire file. No verification, TLS policy, durable checkpoint, or connection limit is removed. The worker's additional buffers are bounded to 1 MiB for checksums, 64 KiB per active body, and libcurl's requested 256 KiB receive buffer. New metrics count actual file writes, checksum reads/bytes, and successful journal publications/time. Journal publication time includes encryption, metadata write/flush, and replacement, while data-flush time remains separate. [Benchmarks](BENCHMARKS.md) record the measured tradeoffs.

## File correctness

The caller chooses an explicit destination. Content-Disposition is parsed only into a sanitized suggestion and never changes that path. Invalid Windows filenames/device paths and excessively long paths are rejected. The destination parent must exist.

Fresh requests create a sibling `.part` with CREATE_NEW and no sharing. Explicit resume opens an existing regular partial exclusively only after loading its protected record, then checks its identity, size and saved range hashes before network reuse. Explicit restart archives old partial/ledger files under unique names. Known-size segmented transfers reserve disk allocation and set a 64-bit length; allocation failure aborts. The file remains open through validation/finalization. After all ranges or streaming bytes complete, the writer checks size and an optional trusted SHA-256, flushes data, and renames by handle with `ReplaceIfExists=FALSE`. A destination appearing during download remains intact. Failure leaves a partial, never an apparently completed file. Recovery details, atomic checkpoint publication, finalization reconciliation, and retry policy are in [recovery](RECOVERY.md).

## HTTP and security policy

- HTTP/HTTPS URLs only, no embedded credentials or netrc credentials.
- At most five redirects; HTTPS cannot downgrade to HTTP. Segments do not follow redirects.
- TLS peer/hostname verification and Schannel's default revocation checks remain enabled. An explicit CA bundle scopes trust to that transfer; no system-store changes.
- `Accept-Encoding: identity`; content decoding disabled; chunk framing decoded. Unsupported encodings, multipart ranges, and ambiguous Content-Length/Transfer-Encoding are rejected.
- Bounded cumulative/per-line headers. Interim responses and redirects reset metadata; trailers cannot redefine accepted representation metadata.
- Error categories and static diagnostics/HTTP codes only. URLs, credentials, query strings, and server-supplied text are not logged. Files never execute automatically.

The behavior follows libcurl's [header callback contract](https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html), Windows' [rename-by-handle contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info), and libcurl's [Schannel verification policy](https://curl.se/docs/sslcerts.html).

## Browser integration and future boundaries

The [queue service](QUEUE.md) owns SQLite and schedules download workers; the [desktop](DESKTOP.md) submits asynchronous commands. The [Chrome extension](CHROME.md) uses a separate origin-restricted `DownshiftNativeHost.exe`, bounded JSON frames, and a current-user restricted named pipe. `PipeServer` performs native preflight on its worker and submits future commands to the queue service. Encrypted prepared entries/receipts persist atomically before Chrome cancellation, and idempotent commit/abort reconcile browser worker restarts. The engine pins preflight's resolved URL and, for safe range-capable strong-ETag resources, the version and known length. Ordinary streaming remains available when safe segmentation is unsupported. No browser cookies/authorization context is imported. `src/main.cpp` owns the pipe after the queue/window; shutdown stops it before releasing queue ownership. A per-user mutex activates an existing application instance.

## General downloads, media and distribution

QueueManager owns category-folder creation and durable state on its service thread. Browser metadata and native inspection identify video/audio/archive/other folders; desktop overrides are explicit destinations. Existing records are never relocated. The compact Qt window hides extra list columns at narrow widths and displays details through asynchronous snapshots. No blocking extraction, network, persistent database or file-write work is added to the GUI thread.

The extension's enabled capture starts with an early browser pause and opens/activates the app. PipeServer prepares an inert encrypted entry/receipt before browser cancellation. After cancellation is verified, commit saves a paused destination-selection record; Start download queues it. Blocked handoffs are held until retry or explicit disable, while post-cancel uncertainty is reconciled by durable receipt to avoid duplicates. This cannot prevent bytes/completions before Chrome's event. No authenticated browser context is copied. See CHROME.md and GENERAL_DOWNLOADS.md for the API/capacity/privacy boundaries.

MediaDialog launches pinned yt-dlp/Deno/FFmpeg asynchronously and owns child processes with a Windows job. Media entries are explicitly excluded from the native scheduler. Submitted source addresses and chosen folders persist encrypted; resolved format links remain transient. A bounded JSON after-move filename identifies the saved local output. ffprobe verifies a video stream/positive duration, then the queue service checks a nonempty regular file inside the chosen folder before Completed. Interrupted jobs become failed history for explicit retry. Media helper per-task caps/concurrency are separate from the native queue's aggregate budget.

Portable packaging copies only product executables, dynamic release libraries, Windows Qt plugins and app-local VC143 runtimes. A manifest records source commit/dirty state and all shipped file hashes. Setup/Uninstall back up and restore only owned per-user integration; history/downloads are untouched. Optional media executables are fetched directly from upstream after checksum verification. The matching release carries complete library source archives and pinned vcpkg recipes/patches for LGPL obligations. Windows CI builds/tests both configurations with actual media helpers, creates a clean package and verifies the extracted application's startup and integration lifecycle before preserving artifacts. Local actual Chrome testing remains separately identified evidence.

## Shared HTTP capability negotiation (0.3.3)

The ordinary-file pipeline is independent of site names and file formats: browser URL/metadata → inert native receipt → save/start window → metadata discovery → range negotiation → transfer → verified atomic completion. No Lightshot host or URL branch exists. This is an original implementation of a standard download-manager architecture; proprietary IDM internals are not represented as known or copied.

A small byte-range request discovers metadata. If a server refuses or malforms that probe, a bounded un-ranged GET obtains headers and aborts before copying its body. The first real range must validate before the parallel pool opens. Every later range still requires an exact strong tag, range geometry and identity encoding. Weak/missing tags, inconsistent ranges, ignored ranges and range-specific refusals switch a fresh transfer to one complete GET after invalidating checkpoints and clearing untrusted range data. Whole responses can decode supported HTTP content encodings; length/compression/framing and optional trusted SHA-256 checks still gate finalization. A webpage substituted for an expected binary response is rejected. Strong version changes continue to fail.

Zero-recovered-byte failed attempts can use full-response fallback; saved verified ranges cannot silently restart. Resume rehashes saved bytes and requires unchanged strong tags, totals and modification metadata. Rotating query credentials on the same resolved origin/path are permitted under those checks; different resource paths/origins stay rejected. Non-resumable streaming responses do not gain safe byte continuation merely because a download completes.

Browser preparation can use a safe Content-Disposition filename when the link provides no explicit user name. MIME information supplies category routing. The user can override the destination/name before Start. Native readiness reports the actual running app version and filename capability, avoiding opaque requests to an old running app after an extension update. Direct request keys remain URL-free in browser persistence; native history and recovery remain encrypted.

## History lifecycle and GUI 2.0 (0.4.0)

Normal storage uses the Windows Local AppData known folder and a fixed Usha Labs/Downshift path. The queue service owns the lease, transactional startup validation, serialized retry and SQLite backup API snapshots. A missing/zero-byte live database with a backup ownership marker is rejected before CREATE. Only validated owned snapshot names rotate; encrypted rows and resume journals remain unchanged. A failed load cannot publish default settings as saved history.

The Qt window distinguishes loading, empty, no-match and unavailable history. Design centralizes palette, icons and shared form/popup styles; DownloadDelegate paints progress/status without per-row widgets. Responsive navigation/action overflow and contextual details consume asynchronous snapshots. Hidden test-window filters keep fixture queues out of the user's visible desktop. The transfer engine is unchanged from 0.3.3. See GUI_2.md.

## Independent range connections (0.4.1)

History follow-up 0.4.2 adds an optional per-executable storage binding, pre-open physical-file identity verification and an authenticated intentional-empty marker. Startup rejects unexplained empty live stores when owned encrypted snapshots retain history. The additive queue_meta table preserves schema-2 HTTP records. See [HISTORY_RECOVERY.md](HISTORY_RECOVERY.md) for the reproduced Desktop/tool storage split and the local recovery procedure.

The range pool disables libcurl multi multiplexing explicitly with CURLPIPE_NOTHING and bounds the idle connection cache to the configured pool size. HTTP/2 remains negotiated normally; each active range uses its own physical connection, and completed connections remain reusable for later ranges. The ordinary single-transfer streaming fallback stays single-connection. Metadata negotiation, hard host/total limits, version/range/TLS validation, encrypted journals, checkpointing and hashes are unchanged. Default multiplexing previously combined eight active easy handles into one physical HTTP/2 connection, despite the UI showing eight ranges. See PERFORMANCE.md and the real TLS transport fixture.

## Torrent Qt boundary (T3)

The normal window receives an independent TorrentEngine and merges copied snapshots with existing HTTP/media rows for presentation. Typed row identifiers dispatch to the appropriate adapter; no torrent scheduling or piece state enters QueueManager. A companion torrents.sqlite resides beside the already resolved HTTP store. Future polling uses Qt timers, and parsing, persistent records, native queries and disk work remain on the torrent worker. Dialogs own only copied data and nonblocking futures.

Metadata-only previews persist their intent and withhold payload requests until file confirmation. Magnet metadata completion pauses the native handle. File priorities, limits and seeding settings are explicit durable commands. An explicit native cache-flush acknowledgement precedes checkpoint serialization; automatically generated storage flush notifications are excluded from this barrier. Window close awaits asynchronous shutdown before teardown. Shared history is a view, not an HTTP database migration.
