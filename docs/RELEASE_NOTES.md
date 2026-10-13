# Downshift 0.3.3 compatibility and history repair

- Shared capability negotiation, one validated range before parallel fan-out, and safe full-response fallback for unreliable ranges/validators.
- Bounded metadata discovery when servers reject range probes; supported whole-response content decoding; accurate final sizes.
- Safe server filenames for unnamed links, explicit-name preservation, version coordination and clear update messages.
- Newest-first history, Latest/Oldest/Size views on every sidebar page, preserved selected IDs and shared rounded pink forms/menus/dropdowns.
- Browser checks use a separate test bridge, leaving the user's normal connection and history alone.

Validation/publication results are recorded after success in DEVELOPMENT_PROGRESS.md. This is an unsigned preview; supported public HTTP/HTTPS transfer does not imply universal authenticated, browser-only or DRM support. No proprietary IDM implementation or general Internet-speed advantage is claimed.

---

# Downshift 0.3.2 reliability repair

- Reject malformed YouTube player IDs and resolve a valid current watch page; return actual errors to the extension.
- Capture trusted direct file links before Chrome starts. Remove owned cancelled fallback entries only after native confirmation.
- Preserve validated, hashed partial ranges on Pause, and reuse them safely on Resume. Earlier pauses without checkpoints require explicit Restart.
- Publish progress during paced callbacks, tune the initial connection pool for small ordinary files, and use fresh HTTP/1.1 on bounded transport retries.
- Preserve downloads, encrypted history, category folders, per-download destinations and the compact pink desktop.

Development users must reload the extension and refresh existing pages; its popup displays Version 0.3.2. Files remain in C:/Users/HP/Desktop/Projects/Downshift, with the existing Desktop shortcut. [Exact-source Windows CI](https://github.com/ushalabs/Downshift-Download-Manager/actions/runs/38435111840) passed both 14-group suites and 13 portable checks; the [v0.3.2 preview](https://github.com/ushalabs/Downshift-Download-Manager/releases/tag/v0.3.2) has verified public asset hashes and a separately downloaded extension ZIP. DEVELOPMENT_PROGRESS.md records each environment and diagnostic failure. Historical release tags/assets are immutable.

Chrome interception cannot universally precede unknown/script-generated downloads. A displayed notification cannot always be suppressed. Server/network conditions limit throughput: the same slow WinRAR source independently delivered about 20 KB/s. This unsigned preview makes no 3 MB/s, universal YouTube/site or clean-VM guarantee. See CHROME.md, GENERAL_DOWNLOADS.md and TESTING.md for scope and evidence.

---

# Downshift 0.3.1 repair preview

Windows 10/11 x64 portable preview with manual unpacked Chrome setup. This repair prioritizes the user's failed YouTube download; it preserves saved files and encrypted history.

## Changes

- Resolve the clicked YouTube player/card and current page after browser navigation; reject unresolved feeds and playlists.
- Route YouTube watch links to video extraction. Ordinary downloads cannot save a watch-page HTML response as a completed video. Existing misleading watch records are marked failed, preserving original files and providing a video retry action.
- List actual playable resolutions. Public YouTube downloads prefer checked HLS video/audio streams, with checked alternatives, and abort on missing fragments.
- Use exact selected heights and actual resolution in filenames. An existing lower-resolution file cannot become a higher-resolution success. Verify the final local video/audio output before saving Completed.
- Modern rounded pink controls, consistent Segoe UI, clearer navigation, custom dropdown/scrollbar styling and an original pink globe/download icon embedded in both Windows and Qt. The compact responsive window remains.
- Retain ordinary-file transfers, category folders, per-download destinations, strict Chrome capture and persistent file/video history.

## Setup and update

Extract the entire Downshift-0.3.1-windows-x64.zip to a permanent writable folder. Run Setup.cmd and use its Desktop shortcut. Load the package's extension folder through chrome://extensions → Developer mode → Load unpacked. Run Enable-video.cmd once for optional pinned upstream helpers.

Existing development users: reload Downshift at chrome://extensions and refresh existing video pages. Updated files alone do not replace scripts already running in those pages. Keep the EXE beside its DLLs. Compare the archive to SHA256SUMS.txt. See PACKAGING.md for moving/uninstalling; user files and history are preserved.

## Verification

Local full Release and Debug suites passed 14/14 before the final exact-height/naming refinement. The final refinement passed real media integration in both configurations, including two separately downloaded and independently probed resolutions. Separate isolated Chrome checks passed 14 in each configuration. Final extension model checks passed 23 and capture state passed 38.

Actual Internet acceptance used the user's public video, The 100 That Changed Pakistan T20 Cricket: real YouTube search-to-watch navigation, trusted extension player click, quality list 1080/720/480/360/240/144, full 144p video+audio download, merge, persisted Completed history, independent file hash and ffprobe duration/streams. It produced 17,905,892 bytes and 713.247 seconds. This does not assert a full 1080p download. Source fingerprints and test chronology are recorded in DEVELOPMENT_PROGRESS.md and evidence/youtube-031-*.json.

The [matching-source Windows CI](https://github.com/ushalabs/Downshift-Download-Manager/actions/runs/38422158389) passed all 14 Release and Debug groups and 13 extracted portable checks. The [v0.3.1 publisher](https://github.com/ushalabs/Downshift-Download-Manager/actions/runs/38423599567) independently verified clean source and all five asset hashes. A separately downloaded public Windows ZIP matched its server digest and passed 13 local portable checks. DEVELOPMENT_PROGRESS.md records these environments separately; previous v0.3.0 remains immutable.

This is an unsigned preview without a store listing, auto-updater or clean-VM claim. Cookies/login are not imported, DRM/protected and unsupported resources remain unavailable, and Chrome's event timing can allow initial bytes or already completed small files. Verification checks container/stream metadata, not every decoded frame. Optional pinned yt-dlp/Deno/FFmpeg executables are fetched directly from upstream, not redistributed. Corresponding library/application sources accompany the release. No Internet/IDM speed advantage is claimed.
