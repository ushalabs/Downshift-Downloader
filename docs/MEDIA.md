# Videos in Downshift

## Installed YouTube repair: native extraction first (2026-10-17)

The current repair restores canonical YouTube extraction as the primary quality and download source. Optional browser descriptors no longer force a stale, incomplete or 360p-only stream over the extractor's full format list. Missing browser streams no longer prevent native extraction. Browser-assisted resolution remains a bounded fallback after an explicit source refusal; its headers and duration apply only when that fallback is actually selected. The selected source mode persists in encrypted video metadata for Pause/Resume. No cookies or authorization are imported.

Live acceptance completed two full public transfers through the actual hidden Qt dialog and isolated queues: FQmqRVK3kHg at 720p (156,336,196 bytes, 997.552 seconds, with offered heights from 144p through 2160p), and jNQXAC9IVRw at its available 240p (603,169 bytes, 18.947 seconds). Both outputs passed local audio/video, duration, selected-resolution and decoded-frame checks; saved frames were inspected visually. The first test deliberately supplied an unusable optional browser descriptor. These are actual public transfers, separately from 56 controlled Chrome checks and the media/queue regressions. They establish the tested sources, not universal access to restricted, removed or unsupported videos.

The verified application and extension repair was applied directly to Program Files with original files retained. No installer was rebuilt. A process already running from an old executable image must exit through the tray before reopening the repaired application. Installed-file/startup verification and a live loaded-process check are distinct evidence; see `evidence/youtube-native-primary-live-local.json` for their status.

The post-Phase 8 user-requested browser update adds a **Download this video** button above visible detected players. YouTube video pages are resolved through the site's extractor, rather than downloading a playback fragment or saving a `blob:` address. Public direct MP4/WebM resources and supported HLS/DASH manifests are resolved too. For a single player, observed master/variant manifests are grouped and the first manifest is preferred over playback fragments. Multiple unrelated players without a direct source remain ambiguous.

Reload the unpacked extension at `chrome://extensions` and then reload the video page. New site-access permissions are needed for the buttons. Disable competing IDM browser integration for a Downshift test; it previously preempted Chrome download events on this machine. The current extension folder remains `C:\Users\HP\Desktop\Projects\Downshift\extension`.

## Desktop workflow

Click the player button, or use **Download video** in the desktop and paste a video page/direct video URL. The app checks available qualities; choose **Best available** or a height, choose the destination folder, and click **Download video**. The separate window displays transfer bytes, speed, progress and estimated time. Separate audio and video are merged into MKV without transcoding; directly downloadable combined files retain their original container. Downloaded media never opens or executes automatically.

The Name field is editable before starting. An edited video name replaces the extractor naming template; the actual media format supplies the extension. Windows-invalid names and reserved device names are rejected, template percent signs are treated literally, and existing files receive a collision suffix rather than being overwritten. Browser save confirmation and Add download also expose a Name field synchronized with Save as. Torrent filenames remain metadata-defined.

Video windows can minimize. Closing with X or Escape hides an active task without cancelling it; use Show video window from its list menu to reopen the same task. The explicit Cancel action still stops the helper and cleans owned incomplete data. A hidden task continues through merge, verification and durable completion, then releases its window. Settings → General → Keep Downshift in the system tray when windows are closed is enabled by default. With a Windows tray available, closing the main window keeps all work and the event loop alive. The tray offers Show Downshift and Exit Downshift; explicit exit warns when active transfers would stop and checkpoints torrent progress before shutdown. If the tray is unavailable, the application uses its normal exit path rather than hiding without a way back.

Video downloads now provide Pause and Resume in the video window and the main list's toolbar, detail control and context menu. Pause stops only the owned helper job and retains its private `.part`/fragment checkpoints; it does not invoke cancellation cleanup. Resume uses yt-dlp's documented [partial-file and fragment continuation](https://github.com/yt-dlp/yt-dlp#download-options), the same history ID, staging identity, source/context, quality selection and output name. Its bounded selection recipe stays inside encrypted history, without arbitrary stored helper arguments or a database schema change. Paused tasks survive closing their view and restarting Downshift. Interrupted new-format tasks are held paused for explicit Resume; older uncheckpointed interrupted records retain their earlier recovery behavior. Cancel or removal still cleans owned incomplete data. Pause is disabled while final merging/verification is finishing. Continuation depends on the source remaining available and supporting its required ranges/fragments; expiry or source errors remain explicit, and final local container/stream verification is still required.

Version 0.3.1 reads the content script's current page address after YouTube navigation, within the sender's verified origin. A specific preview card can supply its video link; a feed alone cannot stand in for a video. Feeds/playlists without playable video formats are rejected before Download is enabled. Pasting a YouTube watch/shorts link into Add download opens the video window. Old ordinary-file watch-page attempts are held with a Retry video explanation; existing files are preserved.

The real public YouTube acceptance test exposed HTTP 403 on advertised direct formats. YouTube transfers now prefer available HLS video/audio at the selected height, with checked direct alternatives. The helper checks the chosen formats and aborts missing fragments instead of silently producing an incomplete file. This follows the [upstream stream/client guidance](https://github.com/yt-dlp/yt-dlp/wiki/PO-Token-Guide); no cookies, account credentials or token-provider plugins are introduced. Site availability can change; actual transfer evidence is separate from a successful format listing.

An explicit height requests that actual resolution. Resolved heights appear in filenames, so changing quality cannot silently reuse an existing lower-resolution file. Valid earlier files remain untouched. Unknown-height direct files keep their previous naming behavior.

These tasks use pinned local yt-dlp, Deno and FFmpeg helpers controlled asynchronously by the native C++/Qt application. Ordinary file downloads continue to use Downshift's libcurl engine. Video jobs now persist in the same encrypted SQLite history: **All downloads** includes active and finished tasks; **Completed** includes verified saved videos; **Needs attention** includes failed/cancelled videos. Records survive reopening. The details show the final path, and **More → Open folder** locates it. Video windows also provide **Open folder** after completion. Earlier versions did not save video history, so existing missing records cannot be reconstructed automatically.

There are up to four video windows and one active helper per window. Same-URL requests reuse a window. Current stream progress/speed appears in both the video window and main list, and video speed contributes to the displayed total. Stream progress can restart for the next audio/video component; it stays below 100% until final verification succeeds. The saved speed limit is copied as a per-video helper limit at startup; concurrent video/file tasks do not share an aggregate budget. Automatic video fragment concurrency is four; fixed settings are bounded at eight. These rates are not evidence about the native file engine's benchmark performance.

**Cancel** stops the helper process tree and leaves partial files for retry where the site permits. Retry repeats extraction to refresh expiring links and uses the helper's continuation support. Closing an active video window asks before stopping it. Keep the app running; tasks are not a background Windows service. Main-list video controls provide **Retry video** and **Cancel**; Pause is unavailable for media tasks. Interrupted videos are retained as failed history with a retry explanation after reopening, and are never scheduled as native file/page downloads. Metadata resolution is bounded at 90 seconds and four MiB, and individual helper socket operations have a 20-second timeout.

Successful helper exit or a finished stream does not establish completion. After merging and moving the output, the app reads a JSON-encoded final filename, runs asynchronous ffprobe with local-file-only access, checks for a video stream and positive recorded duration, then verifies a nonempty regular saved file inside the chosen folder on the queue service. Only then does it persist Completed and display **Saved: [exact path]**. Missing/invalid outputs are failures with retry. This validates the container/stream metadata, not a full decode of every frame or a trusted-source checksum.

## Support boundaries

### Browser-assisted YouTube in 0.4.5

The YouTube overlay now reads the current player's bounded standard media descriptors. When the browser exposes only SABR, a trusted click can make one bounded same-origin Web/HLS player request using Chrome's current visitor context and, when present, its YouTube page session authorization. The cookie and derived authorization stay inside that YouTube page and its same-origin request: neither is exported to the extension worker or native app, written to a cookie file, or saved in history. There are no new cookie permissions, native account imports, account/client retry loops or challenge-solving services. An unsuccessful request has a local 30-second request cooldown.

Only the matched current video ID, duration and validated standard media URLs cross the bridge. Cipher-only formats can be matched to that document's successful standard playback requests by resource ID, itag and content length. Unsigned transport ranges are removed for full-file delivery; signed ranges, UMP/SABR containers, expired sources, mismatched navigation/documents and protected/live players are rejected. Browser-provided standard manifests retain the usual quality discovery. Separate tracks use a minimal internally constructed info document through helper stdin, without another anonymous YouTube watch-page request or a plaintext signed-URL file. UTF-8 output and ASCII-escaped JSON stdin preserve Unicode names.

User confirmation, bounded quality selection, helper cancellation, Pause/Resume and encrypted history remain in place. A fresh browser source can update an idle/paused matching window; Resume saves the refreshed context before transfer. Completion additionally requires audio/video streams and the browser's expected duration within one percent or half a second. Existing ordinary-file and torrent transfers are outside this change. Pasted watch links without browser context still depend on the site's anonymous extractor. A rejected browser session or a player without standard downloadable streams can still be unavailable; universal YouTube/DRM support is not claimed.

### Installed YouTube diagnosis (2026-10-17)

For the reported `FQmqRVK3kHg` failure, the Program Files app and project Release app have identical executable hashes. All twelve installed video-tool files match the pinned checksums and their project counterparts. The installed Deno runtime executes from Program Files, and yt-dlp detects Deno, bundled EJS, FFmpeg and ffprobe. Two sequential, direct metadata probes with retries disabled reproduce HTTP 429 and LOGIN_REQUIRED in both copies. These probes invoke each executable explicitly and do not depend on which app Chrome opens. The user clarified that the earlier project success was before installation, rather than a simultaneous successful comparison.

A private, checksum-verified probe of upstream nightly `2026.10.10.233141`, including its newer default YouTube clients, also receives HTTP 429 and LOGIN_REQUIRED. It was not installed or added to the packaging lock. This confirms the present upstream refusal but does not establish why earlier requests succeeded, nor prove an installer-triggered regression. No successful current public YouTube download or repair is claimed. Cookies/session credentials, normal registration, installed files and real queues remain untouched. Evidence: `evidence/youtube-installed-diagnosis-local.json`. Further repeated public probes were stopped after the same refusal.

An action on a detected player does not mean every stream is downloadable. DRM-protected, subscription/login-only, geo-restricted, live and unsupported resources can be unavailable. DRM bypass is not enabled. Browser cookies and credentials are not imported. Closed shadow roots, canvas-only players, browser internal pages, and videos placed directly into native video fullscreen cannot always expose an overlay to a content script. Normal YouTube fullscreen uses a container and is supported. Public site extraction changes independently of Downshift and can require an explicit helper update.

## Local helper setup

The Windows EXE installer now performs video-tool installation automatically. Keep Internet connected during setup; no separate Enable-video step is required. It verifies both downloaded archives and extracted runtime hashes, reuses already verified installed files and removes completed setup-download archives. The portable ZIP-only workflow below still uses Enable-video.cmd. Chrome extension setup is separate: this unpublished preview requires one-time Load unpacked, with a guide opened from the installer's finish page.

```powershell
./scripts/Install-MediaTools.ps1 -Configuration Release
./scripts/Install-MediaTools.ps1 -Configuration Debug
```

This downloads only pinned upstream release files into ignored `.tools/media`, verifies SHA-256 before installing them next to each local app in `media-tools`, and changes no system PATH. Versions: yt-dlp 2026.08.19, Deno 2.9.7, FFmpeg n8.1.3-18-g46d899d7a0 / BtbN immutable 2026-10-12 build with shared LGPL configuration. Hashes and download URLs are in the script and local `sources.json`. No helpers auto-update or load user config/plugins. No `--exec`, browser-cookie, insecure TLS, or unplayable-format switches are accepted from the extension. Extractor format links stay in process memory; the submitted source address is retained in encrypted history. Errors redact HTTP addresses and are not written to activity logs.

The portable package omits these helper executables. Enable-video.cmd downloads them directly from their pinned upstream releases after verification. Their own upstream licenses apply; they are not relicensed by Downshift. The bundled yt-dlp executable has third-party licenses in addition to the project's Unlicense. Sources: [yt-dlp release and licenses](https://github.com/yt-dlp/yt-dlp/releases/tag/2026.08.19), [YouTube JavaScript requirements](https://github.com/yt-dlp/yt-dlp/wiki/EJS), [Deno release](https://github.com/denoland/deno/releases/tag/v2.9.7), [FFmpeg build](https://github.com/BtbN/FFmpeg-Builds/releases/tag/autobuild-2026-10-12-13-12).

## Verification

`browser.media` checks URL restrictions, YouTube normalization, frame isolation, ambiguous-source fallback, candidate bounds/expiry, and default-on versus saved-off settings. `browser.extension` retains staged-handoff/outage/race checks and checks Chrome-safe installer/script/small candidates. `browser.native` checks the actual native protocol, including rejected media URLs and injected arguments.

`gui.media` creates real MP4 and separate audio/video DASH fixtures, drives the Qt quality/download controls, checks the main history table, persistence/recovery, missing output, native-worker isolation, error/retry and event-loop heartbeat, and independently verifies the direct file's SHA-256 plus both files' audio/video streams and duration through ffprobe. A valid existing download is verified again; an invalid existing output skipped by yt-dlp's no-overwrite behavior must fail. Without local helpers this group explicitly skips with code 77; a skip is not media acceptance. The separate real Chrome test clicks the actual injected button, observes the native Qt dialog, starts its real download, hashes the output, checks completed video history, and verifies button removal after a player disappears. Accepted execution results are recorded in development progress.

## YouTube player URL repair in 0.3.2

The extension previously accepted six-or-more-character YouTube IDs while native extraction required exactly eleven. A malformed player link such as watch?v=undefined could be preferred over a valid current page and produce the user's banner. Both sides now require eleven characters, short links are canonicalized, invalid player links fall back to the actual page, and the native host rejects invalid media addresses before acknowledging a window. This mismatch is reproduced by controlled tests; the exact original user packet was not captured. Refresh existing pages after reloading the extension and confirm its popup version.

Actual Internet tests remain opt-in and identify the fully downloaded resolution separately from the offered quality list. Successful controlled and public tests do not guarantee every video, account-restricted resource, stream or quality will work.


## Embedded players and default quality (2026-10-16)

The extension now recognizes HLS/DASH manifests already fetched by the player, including text/plain or opaque-looking HTTP URLs. A bounded MAIN-world observer inspects only response prefixes, leaves the original fetch/XHR response intact and makes no extra stream requests. Frame/document-scoped observations stay in bounded worker memory. A single visible player can use its observed master manifest even after variants have been requested. Direct YouTube videos continue to use the canonical page extractor. EME-protected players are rejected; no license extraction or DRM bypass is performed.

Only the media request's validated Referer, Origin and User-Agent are carried to the native helper. Cookie and Authorization headers are neither exported nor accepted by the media protocol. The context is retained inside encrypted video history for retry. An updated-extension/old-app mismatch gets a restart instruction through a mediaStreams capability check. This does not change ordinary file capture or torrent transfers.

A real single stream is accepted even when the extractor exposes no alternate format/resolution list. The dialog shows one Default quality option and enables Download video; multiple known resolutions retain the quality menu. It does not invent a downloadable stream when a player page is unsupported. Completion still requires the actual local output and asynchronous stream/container verification.

The supplied MyFlixer and 9anime pages and their embedded-player HTML were reachable during inspection. A bounded isolated browser run did not expose playable manifests, so successful downloading from those exact live sources is not claimed. Private fixtures exercise cross-origin iframe/blob players, generic-MIME HLS master/variant requests, required referrers, unknown/default resolution, chosen resolutions and independently verified audio/video output. No universal Internet-video guarantee is possible from these checks.
# YouTube 0.4.6 follow-up

Comparison with commit `d45264d` confirms that the committed implementation and
the installer use the same pinned yt-dlp, Deno and FFmpeg versions. This does not
prove that a source that worked earlier remains available to anonymous requests.

The 0.4.5 browser repair had a reproducible separate defect: it required a watch
ID in the document URL. A home-feed player could therefore lose its descriptor
and silently trigger anonymous native extraction. Version 0.4.6 binds metadata
to the actual clicked video/player, accepts serialized player responses, and
can prepare metadata from the player's current ID and finite duration when its
response object is absent. Watch-page/player mismatches, live streams and
protected media remain rejected. Same-resolution alternate tracks are retained
and successful browser-observed URLs take precedence over pre-transformation
player URLs. A failed browser resolution reports its failure in the overlay
instead of opening another anonymous HTTP 429 lookup.

Real private Chrome acceptance covers these paths using a local HTTPS server
and the actual webRequest events. Debugger-fulfilled responses did not emit
those events and were unsuitable for the observed-stream assertion; they were
replaced, not counted as successful network acceptance. The fixture's explicit
certificate pin is confined to its private Chrome invocation. No production
TLS setting, normal profile, credentials or active download is changed. These
checks do not establish a successful public signed-in YouTube transfer or the
exact browser state behind the user's screenshot.

The subsequent installed hotfix completes the helper's extractor identity,
checks direct browser formats during quality discovery, and uses explicit labels
such as 360p when resolution is known. A combined fallback alone no longer ends
quality preparation prematurely. Rejected sources cannot publish completion or
trigger an anonymous watch-page retry. The original missing-field exception was
independently reproduced before checking the correction. The helper's error
handling contract is visible in its pinned [upstream implementation](https://github.com/yt-dlp/yt-dlp/blob/2026.08.19/yt_dlp/YoutubeDL.py).
