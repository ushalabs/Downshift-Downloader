# Downshift architecture

## Current execution boundaries

The native Windows application uses C++20, MSVC v143, Qt 6 Widgets, libcurl multi with Schannel TLS/HTTP2, and SQLite's C API. Qt and the MSVC runtime are dynamically linked. CMake's x64 Visual Studio generator and a pinned vcpkg baseline build Debug and Release. HTTP/3 is not selected.

`src/main.cpp` owns `QApplication` and `CurlRuntime`, validates linked dependencies, and opens the foundation `MainWindow`. The window does not call the transfer engine. The Phase 2 console executable, `DownshiftDownload.exe`, owns its own `CurlRuntime` and calls the engine on its main worker thread. The blocking API must remain off the GUI thread when desktop controls are added in Phase 6. Persistent queue/database work is Phase 5.

## Implemented engine

| Component | Responsibility |
|---|---|
| `core/DownloadEngine/DownloadEngine.*` | Validated request, curl multi session, owned easy handles, callbacks, bounded polling, fallback, completion |
| `core/DownloadEngine/HttpResponse.*` | Separate response headers, strict lengths/ranges, strong validators, safe filename suggestions |
| `core/SegmentScheduler/SegmentScheduler.h` | Lazy, fixed-size, nonoverlapping 64-bit ranges |
| `core/FileWriter/FileWriter.*` | Exclusive partial handle, disk allocation, positional writes, CNG SHA-256, safe rename |
| `src/DownloadCli.cpp` | Development download client and secret-safe errors |

An actual `Range: bytes=0-0` GET probes support. `Accept-Ranges` is advisory only. Segmentation requires a valid `206`, exactly `0-0/total`, untransformed content, a one-byte body, and a strong quoted ETag. A missing/weak ETag causes a fresh complete streaming request. A server returning `200`, an empty representation returning `416 bytes */0`, or a malformed range probe also uses streaming. Other HTTP errors fail without a completed destination.

The fixed Phase 2 policy uses up to four connections and 4 MiB chunks. The scheduler assigns ranges once; completed handles receive the next range. Only active range objects are allocated, bounding memory independently of file size. Each segment sends `If-Match`, requires `206`, checks exact first/last/total, verifies the ETag, and enforces its byte count. A changed representation, incorrect range, segment redirect, missing metadata, or short/oversized body aborts. The engine counts verified bytes across all ranges before finalization.

If a segment instead returns `200` with the same strong ETag, all active handles are detached and destroyed before truncating the partial. A conditional full GET then writes from byte zero. Streaming must return `200`, untransformed content, and no `Content-Range`; a known strong ETag must still match. Content-Length, chunked framing, and EOF-delimited responses are supported. Only an independently supplied checksum can establish the intended contents of an unknown-length response beyond transport completion.

Callbacks capture exceptions into `exception_ptr`; no C++ exception crosses libcurl's C boundary. Easy handles, header lists, URL objects, multi handles, file handles, and hash objects have explicit RAII ownership. `curl_multi_poll` waits at most 100 ms per iteration. A stop token provides cooperative worker cancellation and leaves a partial. This is a shutdown primitive, not persisted pause/resume. Network/disk callbacks occur on the owning worker; positional writes are serialized while connections progress concurrently.

## File correctness

The caller chooses an explicit destination. Content-Disposition is parsed only into a sanitized suggestion and never changes that path. Invalid Windows filenames/device paths and excessively long paths are rejected. The destination parent must exist.

The writer creates a sibling `.part` with `CREATE_NEW` and no sharing, never modifying an existing partial. Known-size segmented transfers reserve disk allocation and set a 64-bit length; allocation failure aborts. The file remains open through validation/finalization. After all ranges or streaming bytes complete, the writer checks size and an optional trusted SHA-256, flushes data, and renames by handle with `ReplaceIfExists=FALSE`. A destination appearing during download remains intact. Failure leaves a partial, never an apparently completed file. Recovery metadata and reopening partials are Phase 4.

## HTTP and security policy

- HTTP/HTTPS URLs only, no embedded credentials or netrc credentials.
- At most five redirects; HTTPS cannot downgrade to HTTP. Segments do not follow redirects.
- TLS peer/hostname verification and Schannel's default revocation checks remain enabled. An explicit CA bundle scopes trust to that transfer; no system-store changes.
- `Accept-Encoding: identity`; content decoding disabled; chunk framing decoded. Unsupported encodings, multipart ranges, and ambiguous Content-Length/Transfer-Encoding are rejected.
- Bounded cumulative/per-line headers. Interim responses and redirects reset metadata; trailers cannot redefine accepted representation metadata.
- Error categories and static diagnostics/HTTP codes only. URLs, credentials, query strings, and server-supplied text are not logged. Files never execute automatically.

The behavior follows libcurl's [header callback contract](https://curl.se/libcurl/c/CURLOPT_HEADERFUNCTION.html), Windows' [rename-by-handle contract](https://learn.microsoft.com/en-us/windows/win32/api/winbase/ns-winbase-file_rename_info), and libcurl's [Schannel verification policy](https://curl.se/docs/sslcerts.html).

## Future boundaries

Adaptive connections/chunks and speed sampling are Phase 3. Resume/retries/recovery are Phase 4. SQLite queues/history/settings are Phase 5. GUI controls and queued progress are Phase 6. Chrome Manifest V3, origin-restricted Native Messaging, and authenticated local IPC are Phase 7. Optimizations, packaging/compliance, and release follow in Phases 8-10. Those modules remain reserved directories. Approval is required before each phase.
