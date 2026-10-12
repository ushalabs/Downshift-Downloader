# Downshift architecture

## Current execution boundaries

The native Windows application uses C++20, MSVC v143, Qt 6 Widgets, libcurl multi with Schannel TLS/HTTP2, and SQLite's C API. Qt and the MSVC runtime are dynamically linked. CMake's x64 Visual Studio generator and a pinned vcpkg baseline build Debug and Release. HTTP/3 is not selected.

`src/main.cpp` owns `QApplication` and `CurlRuntime`, validates linked dependencies, and opens the foundation `MainWindow`. The window does not call the transfer engine. The Phase 2 console executable, `DownshiftDownload.exe`, owns its own `CurlRuntime` and calls the engine on its main worker thread. The blocking API must remain off the GUI thread when desktop controls are added in Phase 6. Persistent queue/database work is Phase 5.

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

## Future boundaries

Phase 4 recovery/retries are implemented through the worker/console interface. SQLite queues/history/settings are Phase 5. GUI controls and queued progress are Phase 6. Chrome Manifest V3, origin-restricted Native Messaging, and authenticated local IPC are Phase 7. Optimizations, packaging/compliance, and release follow in Phases 8-10. Those modules remain reserved directories. Approval is required before each phase.
