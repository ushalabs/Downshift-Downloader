# Pause, resume, and recovery

Phase 4 adds recovery to the native development client. The desktop window remains the foundation shell until Phase 6. The worker API exposes `DownloadControl::requestPause()`; the console maps Ctrl+C/Ctrl+Break to that request. Pause exits the worker cleanly and retains a checkpoint. Start a new worker with `ExistingDownload::Resume` to continue.

## Use the console client

From the project folder:

```powershell
./build/windows-msvc/Release/DownshiftDownload.exe 'https://example.com/file.zip' "$env:USERPROFILE\Downloads\file.zip" --progress
```

Press Ctrl+C to pause. A paused download returns exit code 3. Completion returns 0; failure returns 1; usage without a request returns 2. Window closure or forced termination may skip the final pause checkpoint; already published checkpoints still support recovery.

Use the identical URL and destination to resume after a pause, process crash, failed transfer, or restart:

```powershell
./build/windows-msvc/Release/DownshiftDownload.exe 'https://example.com/file.zip' "$env:USERPROFILE\Downloads\file.zip" --resume --progress
```

Downshift checks the saved file's identity, length, and completed-range checksums, then probes the remote resource again. It requires the same strong ETag, total length, resolved URL, and saved Last-Modified value when present. The probe and subsequent ranges send `If-Match`. Saved ranges are skipped; unfinished ranges are requested again in full. You can change connection/chunk settings for the remaining work. The expected checksum saved at creation remains binding; omitting it on resume retains it, and supplying a different one is refused.

When safe resumption is unavailable, explicitly restart:

```powershell
./build/windows-msvc/Release/DownshiftDownload.exe 'https://example.com/file.zip' "$env:USERPROFILE\Downloads\file.zip" --restart --progress
```

Restart archives existing partial/metadata files under a unique `Downshift.previous-ID.part` and matching `.part.resume` name in the destination folder, then starts a new download. It preserves the old bytes and never replaces an existing completed destination. A crash between the two archive renames can leave an incomplete archive pair; those archives are not automatically resumed. Use a new destination if a completed file already exists.

`--pause-after-mib N` is a development checkpoint aid. Segmented transfers pause after at least N MiB are verified, including recovered bytes; streaming transfers pause after N MiB are written. Progress notifications may overshoot the threshold, and a very fast transfer may complete before a downloading-stage notification. Ctrl+C and the worker control apply independently of this aid.

## What is saved

A sibling `.part.resume` contains a versioned, bounded recovery ledger: random download ID, original/resolved URLs, destination, partial file identity, expected length/checksum, strong ETag, Last-Modified, timestamps, resume capability, state, and sorted completed ranges with SHA-256 digests. State distinguishes active, paused, failed, and finalizing work. The original `.part` holds payload bytes; a transient `.part.lock` prevents two workers from creating, resuming, or restarting the same task. Windows removes the lock when its last owning handle closes, including process termination.

The whole ledger is protected with user-scoped [Windows DPAPI](https://learn.microsoft.com/en-us/windows/win32/api/dpapi/nf-dpapi-cryptprotectdata). URLs/query tokens are encrypted on disk. The client does not accept or save cookies or authorization headers. DPAPI normally requires the same Windows user/profile and machine; roaming profiles can differ. It does not protect against software already running as that user. Do not assume a ledger is portable to another account/computer.

Completed ranges enter the ledger only after validated response headers, exact byte counts, local hashing, and a successful partial-file flush. The encrypted ledger is written to a fresh temporary file, flushed, and published by replacement in the same directory. A crash can leave an encrypted `.downshift-ID.tmp`; it is ignored for recovery. The previous published ledger remains authoritative. There is no promise that storage hardware honors flushes during physical power loss; hash/identity checks refuse damaged saved data.

The scheduler lazily fills gaps around completed ranges with 64-bit offsets. It never reuses completed byte intervals for new writes. A retry replaces only its own unfinished interval, after the prior request handle has been detached. File and metadata handles reject reparse points and hard links. The destination is validated before recovery-sidecar operations.

Before final rename, the finalizing ledger is published. If interruption happens before rename, a fully saved segmented file can resume and finalize without fetching its completed ranges. If it happens after rename but before ledger deletion, resume verifies the existing destination's identity and all saved range hashes (or the full streaming digest), then cleans the ledger without network access. A mismatching existing destination is preserved and rejected. `cleanup_pending` in final JSON indicates that completion succeeded but ledger cleanup failed; a later explicit resume can reconcile it.

## Retries

The default is three retries per request, in addition to its initial attempt. `--retries 0..10` configures the bound; zero disables retries. Probe, segmented, and streaming requests share this policy. Transient DNS/connect/send/receive failures, timeouts, incomplete transport bodies, and HTTP 408/429/500/502/503/504 are eligible. Invalid requests, authorization/precondition failures, TLS verification errors, malformed ranges, changed validators, bad checksums, and file errors fail without blind retry.

Backoff starts at 500 ms, doubles per attempt, and uses jitter between half and the full delay, capped at 60 seconds by default. [Curl's Retry-After information](https://curl.se/libcurl/c/CURLINFO_RETRY_AFTER.html) supplies a minimum delay for both delta seconds and HTTP dates. A server delay beyond the configured wait ceiling refuses retry instead of shortening that delay. HTTP 429/503 also defer new assignments to this host while existing requests drain. Backoff polls pause/cancellation every 25 ms; network polling remains bounded at 100 ms.

Segment retries retain validated completed ranges and overwrite the failed range from its start. Streaming retries truncate and restart the whole response, so bodies from separate attempts are never concatenated. Written progress may decrease when failed bytes are discarded; aggregate accepted-payload counters remain monotonic and include retry writes. Result JSON adds `retries`, `recovered_bytes`, and `cleanup_pending`; progress adds recovered bytes and resume availability. Average final goodput excludes recovered bytes from its numerator. Local revalidation, per-range hashing, checkpoint flushes, retry delays, and allocation count toward the current worker's transfer time.

## Limits and verification

Strong ETags and actual range support are required for partial reuse. Weak/missing validators, streaming, legacy Phase 2/3 partials without a valid ledger, corrupted metadata, changed resources, and modified/replaced partials require an explicit restart. Last-Modified alone is not used as proof of file identity. No credentials or trust settings are weakened to enable resume.

The ledger supports at most 100,000 completed ranges, 32 KiB per text field, and 32 MiB of encrypted input. Exceeding these bounds fails safely with the last published checkpoint retained. Ordinary filename/path limits and available disk space still apply; restart archives also require disk space. Pause is cooperative and can wait for a synchronous hash or flush operation.

Native checks include encrypted-ledger roundtrips, tamper/bounds rejection, exclusive ownership, retry arithmetic, gap scheduling, and reopened sparse-file hashes above 4 GB. Controlled recovery scenarios exercise repeated pauses, forced process termination, remote changes before/during resume, local corruption/substitution/hard links, malformed metadata, explicit restart, finalization gaps, transient faults, exhaustion, Retry-After, backoff interruption, and TLS rejection. These are actual local correctness tests; a real Windows reboot, physical power loss, disk-full fault injection, HTTP/2 wire recovery, and GUI pause controls are not claimed as tested. See [test results](DEVELOPMENT_PROGRESS.md).
