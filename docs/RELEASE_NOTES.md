# Downshift 0.3.0 preview

Windows 10/11 x64 portable release. Original source is MIT; this is an unsigned preview with manual unpacked Chrome setup.

## Changes

- Manage ordinary files and supported video tasks together in persistent encrypted history.
- Automatically suggest Downloads/Videos, Audios, Zip and Programs; allow a different location for every download.
- Open/activate Downshift for Chrome handoffs, then show a save-location window before native transfer.
- Hold unsupported/native-unavailable downloads paused while capture is enabled; explicit disable restores Chrome.
- Support public streaming servers alongside safe segmented downloads.
- Add a compact responsive pink/plum desktop, with extra list details at wide sizes.
- Add player download actions, quality selection and verified saved-video completion/history.
- Deliver app-local runtime dependencies, per-user shortcut/native-host setup, ownership-aware uninstall and complete corresponding library sources.

## Install

Download Downshift-0.3.0-windows-x64.zip and extract the entire archive to a permanent writable folder. Run Setup.cmd and open the Desktop shortcut. Load its extension folder through chrome://extensions → Developer mode → Load unpacked. Run Enable-video.cmd once for optional upstream video helpers. Existing development users can keep their current Desktop shortcut and reload the project's extension after updating.

See PACKAGING.md for uninstall/moving and GENERAL_DOWNLOADS.md for destination choices. Uninstall preserves saved files and encrypted history. Do not copy the EXE away from its DLLs. Compare downloads against the release's SHA256SUMS.txt; GitHub asset digests independently identify uploaded files.

## Verification and boundaries

The [matching Windows Actions run](https://github.com/ushalabs/Downshift-Download-Manager/actions/runs/38402993102) passed all 14 Release and Debug groups with video helpers installed, plus 13 portable checks. The gated publisher independently checked source and artifact hashes before publishing [v0.3.0](https://github.com/ushalabs/Downshift-Download-Manager/releases/tag/v0.3.0) from eeeb0129cb66a3cb9b7b25c74f0199ef95c171bc. An independent download of the public Windows ZIP matched its hash and passed all 13 portable checks locally. Separate actual Chrome checks also passed locally. DEVELOPMENT_PROGRESS.md and evidence/phase10-*.json distinguish each environment and retain source, run URLs and final asset hashes.

Chrome may receive bytes or complete a small file before its extension event. Cookies/authentication are not imported. Browser-only, unsafe, incognito, private/protected/DRM and unsupported resources cannot be universally captured or downloaded. Use explicit capture-off for resources Chrome must handle. Detected video controls are not a guarantee that every site's stream is available. Media tasks have a separate helper concurrency/rate budget. A video completion validates file/stream metadata, not all decoded frames.

The recorded 180-sample Phase 8 benchmark matrix is controlled cached loopback HTTP/1.1, not Internet throughput or an IDM comparison. There is no installer signature, store distribution, automatic update or clean-VM claim. Optional yt-dlp/Deno/FFmpeg binaries are fetched from their pinned upstream releases and are absent from the portable archive. Dependency-source and application-source ZIPs accompany the binary for source/license availability.
