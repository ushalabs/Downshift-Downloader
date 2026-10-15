# Browser response delivery — local 0.4.4

The general capture path manages new downloads reported by Chrome's downloads API, including generated blob/data files, session-bound responses and POST downloads. It selects a transport from the actual browser response instead of guessing file types or treating every source as a public GET. The application and unpacked extension must both be 0.4.4; reload the extension and refresh existing pages after updating. The public v0.3.3 portable preview is unchanged.

The 0.4.4 follow-up rejects unknown completed history from metadata or restoration events. Tiny late completions require an actual fresh creation/filename event; durable pending receipts continue to recover. Completed-ID tombstones prevent delayed duplicate events from recapturing a delivered file after worker restart. A historical Chrome file is never imported merely because it exists in download search results.

## Transport selection

Chrome resolves the original request first. An early pause and asynchronous filename gate retain that response while Downshift accepts it. A bounded, transient response-header cache records strong ETags for GET responses; POST requests invalidate that URL's cached GET proof.

For a public representation with a matching strong validator, the native preflight also checks size and media type before the existing curl engine takes over. Its segmented transfer, TLS, resume and completion checks remain unchanged. If that equivalence cannot be established, Downshift receives the original browser response instead of attempting another unauthenticated GET. This preserves cookies, original methods/bodies, generated data and resolved blob references without exporting browser cookies or POST bodies to the application.

Browser-owned responses necessarily use the browser's request machinery as transport. Downshift owns destination selection, queue/history, Pause/Resume/Cancel decisions and verified final delivery. This does not remove Chrome's internal network stack or claim access to browser operations that its extension APIs do not expose. Browser security verdicts remain enforced; unsafe files are not silently approved.

## Receiving and completion

- A separate browser-transfer flag uses the existing encrypted queue payload and durable receipt. There is no SQL migration, database replacement or history reset.
- Commit explicitly attests `browserTransport`; it must not falsely assert `browserCancelled` or `browserNotStarted`. Type mismatches are rejected by the native server.
- The existing native save dialog chooses the actual destination. Chrome's suggested filename is retained transiently and sanitized without dropping its extension. A file-extension allowlist is no longer used to intercept page clicks.
- Where its filename callback is available, Chrome receives into `Downloads/Downshift Transfers/<random key>/payload.<extension>`. Delayed adoption can use the existing file associated with that exact Chrome download ID. Browser paths enter native persistence only through encrypted queue records.
- Progress and native controls follow the original download. Native admission limits govern when queued responses start. A persistent Native Messaging connection avoids spawning a helper per status update and keeps the active MV3 worker available.
- Native copying waits for Chrome's full-response completion: file length/bytesReceived are not treated as a contiguous verified prefix because the browser can preallocate or write ranges out of order.
- An off-GUI worker reads in bounded 1 MiB buffers, rejects linked/reparse source files and unsafe paths, uses exclusive destination partials, compares source/copy SHA-256 and exact size, flushes and atomically publishes without replacing a destination.
- Only verified native completion permits deleting the corresponding browser transport file and erasing its Chrome history entry. Cancelling/removing history does not permit deleting the only complete received copy. Cancelled full copies remain available for recovery.
- A small file that completed before normal event capture can be adopted directly from its Chrome download item; it is not fetched again from an expired URL.

The extension suppresses the normal download UI while capture is enabled using Chrome's downloads.ui permission. Switching capture off restores it. Existing transfers already accepted by Downshift remain managed there; uncommitted requests can return to normal browser behavior only on explicit disable.

## Recovery and storage

Pause/Resume uses the existing Chrome download rather than recreating its URL or POST body. The queue stores its received-file path and completion size in encrypted fields. App restart retains paused entries. Resume checks any existing native partial against the actual local source prefix before reuse. Explicit Restart archives the previous partial through the existing recovery mechanism. A fully received local source can finish after restart without its browser worker.

If the original response is genuinely nonresumable or its temporary bytes are deleted, it can fail like an ordinary interrupted transfer; the app reports the actual error. No universal recovery of an expired in-memory object is promised. Browser/extension forced termination, policy restrictions, competing download managers and browser operations outside the downloads API remain limits. Initial request bytes can arrive before an extension event.

Browser-mode transfers temporarily need both the browser's received file and the native destination copy, potentially close to twice the payload's disk space. They do not turn browser-generated bytes into parallel HTTP ranges or automatically inherit curl's network pacing. Complete/cancelled recovery artifacts must not be bulk-deleted. Downloaded programs never execute automatically.

The SQL schema, storage binding, DPAPI account boundary and existing HTTP/media history are retained. Optional browser fields require the updated reader; do not downgrade the live queue to an old binary that cannot interpret those records. No cookies, POST bodies or full data-URI payloads are copied into extension storage or diagnostics. No telemetry/upload is added.

## Verification scope

Controlled native checks cover blob/data/HTTP source descriptors, complete/empty files, independent final hashes, wrong lengths, changed prefix rejection, archival restart, preallocated incomplete sources, Pause/restart retention, fully received local recovery and invalid protocol/type/path input. Actual isolated Chrome checks cover public/native downloads, cookie-only GET, original POST response bytes, small generated files, revoked blobs, data URLs, redirects, unlisted suffixes, destination selection, owned transport cleanup and app restart during Pause/Resume. Existing media and preflight MV3/browser restart checks remain.

Several development attempts to reload the unpacked extension itself in the automated browser did not yield a usable replacement worker; they are excluded from acceptance. The controlled app-restart check is a separate executed lifecycle test, not a claim that that failed extension-reload experiment passed. Public internet checks, local fixture checks and normal-history preservation are recorded separately in DEVELOPMENT_PROGRESS.md and evidence/browser-response-*.

References: [Chrome downloads and filename completion](https://developer.chrome.com/docs/extensions/reference/api/downloads), [Native Messaging lifecycle](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging), [IDM's published integration options](https://www.internetdownloadmanager.com/support/options.html). These describe public interfaces/behavior, not a verified reproduction of IDM's proprietary implementation.
