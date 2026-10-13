# Videos in Downshift

The post-Phase 8 user-requested browser update adds a **Download this video** button above visible detected players. YouTube video pages are resolved through the site's extractor, rather than downloading a playback fragment or saving a `blob:` address. Public direct MP4/WebM resources and supported HLS/DASH manifests are resolved too. When several ambiguous network resources exist in one frame, the page extractor is used instead of guessing a different player's stream.

Reload the unpacked extension at `chrome://extensions` and then reload the video page. New site-access permissions are needed for the buttons. Disable competing IDM browser integration for a Downshift test; it previously preempted Chrome download events on this machine. The current extension folder remains `C:\Users\HP\Desktop\Projects\Downshift\extension`.

## Desktop workflow

Click the player button, or use **Download video** in the desktop and paste a video page/direct video URL. The app checks available qualities; choose **Best available** or a height, choose the destination folder, and click **Download video**. The separate window displays transfer bytes, speed, progress and estimated time. Separate audio and video are merged into MKV without transcoding; directly downloadable combined files retain their original container. Downloaded media never opens or executes automatically.

These tasks use pinned local yt-dlp, Deno and FFmpeg helpers controlled asynchronously by the native C++/Qt application. Ordinary file downloads continue to use Downshift's libcurl engine. Video jobs now persist in the same encrypted SQLite history: **All downloads** includes active and finished tasks; **Completed** includes verified saved videos; **Needs attention** includes failed/cancelled videos. Records survive reopening. The details show the final path, and **More → Open folder** locates it. Video windows also provide **Open folder** after completion. Earlier versions did not save video history, so existing missing records cannot be reconstructed automatically.

There are up to four video windows and one active helper per window. Same-URL requests reuse a window. Current stream progress/speed appears in both the video window and main list, and video speed contributes to the displayed total. Stream progress can restart for the next audio/video component; it stays below 100% until final verification succeeds. The saved speed limit is copied as a per-video helper limit at startup; concurrent video/file tasks do not share an aggregate budget. Automatic video fragment concurrency is four; fixed settings are bounded at eight. These rates are not evidence about the native file engine's benchmark performance.

**Cancel** stops the helper process tree and leaves partial files for retry where the site permits. Retry repeats extraction to refresh expiring links and uses the helper's continuation support. Closing an active video window asks before stopping it. Keep the app running; tasks are not a background Windows service. Main-list video controls provide **Retry video** and **Cancel**; Pause is unavailable for media tasks. Interrupted videos are retained as failed history with a retry explanation after reopening, and are never scheduled as native file/page downloads. Metadata resolution is bounded at 90 seconds and four MiB, and individual helper socket operations have a 20-second timeout.

Successful helper exit or a finished stream does not establish completion. After merging and moving the output, the app reads a JSON-encoded final filename, runs asynchronous ffprobe with local-file-only access, checks for a video stream and positive recorded duration, then verifies a nonempty regular saved file inside the chosen folder on the queue service. Only then does it persist Completed and display **Saved: [exact path]**. Missing/invalid outputs are failures with retry. This validates the container/stream metadata, not a full decode of every frame or a trusted-source checksum.

## Support boundaries

An action on a detected player does not mean every stream is downloadable. DRM-protected, subscription/login-only, geo-restricted, live and unsupported resources can be unavailable. DRM bypass is not enabled. Browser cookies and credentials are not imported. Closed shadow roots, canvas-only players, browser internal pages, and videos placed directly into native video fullscreen cannot always expose an overlay to a content script. Normal YouTube fullscreen uses a container and is supported. Public site extraction changes independently of Downshift and can require an explicit helper update.

## Local helper setup

```powershell
./scripts/Install-MediaTools.ps1 -Configuration Release
./scripts/Install-MediaTools.ps1 -Configuration Debug
```

This downloads only pinned upstream release files into ignored `.tools/media`, verifies SHA-256 before installing them next to each local app in `media-tools`, and changes no system PATH. Versions: yt-dlp 2026.08.19, Deno 2.9.7, FFmpeg n8.1.3-18-g46d899d7a0 / BtbN immutable 2026-10-12 build with shared LGPL configuration. Hashes and download URLs are in the script and local `sources.json`. No helpers auto-update or load user config/plugins. No `--exec`, browser-cookie, insecure TLS, or unplayable-format switches are accepted from the extension. Extractor format links stay in process memory; the submitted source address is retained in encrypted history. Errors redact HTTP addresses and are not written to activity logs.

These helpers are development dependencies, not a distributable package. Their upstream notices and source/relinking obligations join Qt/dependency compliance before any later binary release. The bundled yt-dlp executable has third-party licenses in addition to the project's Unlicense. Sources: [yt-dlp release and licenses](https://github.com/yt-dlp/yt-dlp/releases/tag/2026.08.19), [YouTube JavaScript requirements](https://github.com/yt-dlp/yt-dlp/wiki/EJS), [Deno release](https://github.com/denoland/deno/releases/tag/v2.9.7), [FFmpeg build](https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2026-10-12-13-12).

## Verification

`browser.media` checks URL restrictions, YouTube normalization, frame isolation, ambiguous-source fallback, candidate bounds/expiry, and default-on versus saved-off settings. `browser.extension` retains staged-handoff/outage/race checks and checks Chrome-safe installer/script/small candidates. `browser.native` checks the actual native protocol, including rejected media URLs and injected arguments.

`gui.media` creates real MP4 and separate audio/video DASH fixtures, drives the Qt quality/download controls, checks the main history table, persistence/recovery, missing output, native-worker isolation, error/retry and event-loop heartbeat, and independently verifies the direct file's SHA-256 plus both files' audio/video streams and duration through ffprobe. A valid existing download is verified again; an invalid existing output skipped by yt-dlp's no-overwrite behavior must fail. Without local helpers this group explicitly skips with code 77; a skip is not media acceptance. The separate real Chrome test clicks the actual injected button, observes the native Qt dialog, starts its real download, hashes the output, checks completed video history, and verifies button removal after a player disappears. Accepted execution results are recorded in development progress.
