# Downshift GUI 2.0

The pre-T4 desktop change uses the existing Qt Widgets application and transfer engines. It does not publish a release or change the SQLite schema.

## Design and interaction

`src/gui/Design.*` centralizes white light-theme surfaces and dark slate colors, pink accents, spacing and code-rendered icons. The main window keeps sidebar filters and counts, four live statistic cards, a searchable/sortable mixed download table and collapsible details with an explicit close button and directional expand/collapse icons. The redundant summary and bottom status bar are removed; headings align left and selected rows have no dotted name outline. Binary units use KiB/MiB/GiB. Data downloaded sums the counters retained in current task records; it is not a lifetime network-traffic or disk-usage meter. Low-priority columns move out of smaller windows; compact navigation and the More menu keep actions available. Completed files still require confirmation before opening.

The dashboard has one Add download action. Video capture and retry dialogs remain available. Drops open a confirmation form; they do not silently start transfers. Torrent overview, files, trackers, peers and settings retain their native dialogs.

`modernCombo` supplies a common list delegate, eight-item limit, frameless popup without the native shadow, screen bounds and a width tied to the control. The shared popup paints one rounded surface, uses a list-only combo style and reapplies its native corner mask after Qt finishes resize/style events. This removes the rectangular backing, restored square frame and obsolete scroll arrows. Native Qt keyboard and dismissal behavior remain in use. Numeric torrent fields retain exact ranges and custom values with their spin arrows hidden. Settings use scrollable categories, live theme preview and explicit Save/Cancel; Cancel restores the saved theme.

The `Notification` widget replaces one floating message at a time, avoiding layout movement and unbounded queues. Success lasts 3 seconds, information 4 seconds, warning 6 seconds. Critical errors remain until dismissed or replaced. Timers belong to the widget. No animation runs continuously.

## Details and video previews

Details use actual task counters, source, destination, timestamps, resume support and errors. Connections and task status show available data; they are not fabricated segment maps or historical logs. Torrent details add selected bytes, uploads, peers, seeds, ratio and seeding state.

Optional provider-independent video metadata lives inside the existing encrypted JSON payload. It includes the original page, title, thumbnail, duration, selected/actual height and output format when supplied by the media helper. Older records and ordinary video files have a fallback. Completed local videos can supply a real first-frame preview through the existing asynchronous FFmpeg helper when no usable metadata thumbnail is available. No provider-specific thumbnail is guessed or page scraped for decoration. Clicking the source explicitly opens the original page in the default browser. Signed queries are excluded from the displayed source.

Preview images use asynchronous Qt Network, the Windows Schannel TLS backend and bounded decoding off the GUI thread. HTTPS metadata URLs must be credential-free; literal local/private IPv4 destinations and local hostnames are rejected. Redirects fall back rather than fetching an unvalidated target. Requests time out after 7 seconds and responses are limited to 2 MiB, an image content type and bounded image dimensions. The in-memory cache is bounded. There is no embedded browser/player and no cookie import from Chrome. A failed preview never changes download state. Metadata requests and image storage have real network/memory costs; thumbnails are requested only for an expanded selected video.

The pinned Qt dependency now enables Network, JPEG, PNG and the WebP image-format plugin alongside the existing Widgets/Thread modules. Development and portable staging copy the JPEG, WebP and Schannel plugins, JPEG runtime and the complete WebP/demux/mux/SharpYUV runtime chain. The vcpkg baseline is unchanged. Distribution still requires matching notices, sources and the separate packaging checks.

## Incomplete data lifecycle

The desktop native engine discards its still-open incomplete file and recovery journal after an exhausted failure or cancellation. Pause and application shutdown preserve resume data. Removing an inactive incomplete task checks its encrypted journal and physical file identity before deletion. A replacement, linked, busy or unowned partial is retained with an actionable error, and its history is not silently erased. Restart removes the owned old partial instead of creating another archived copy. Completed destinations survive history removal.

Videos download into a unique staging directory whose physical identity is encrypted with the task. Helpers stop before cleanup; failures, cancellation and interrupted startup clean that directory on the queue worker. Successful ffprobe verification precedes publication into the chosen folder without overwriting an existing file. Duplicate completed filenames receive a numbered suffix. Pre-existing videos in that folder are preserved. Older tasks without a provable staging owner are not treated as permission to erase that folder.

Torrent payload files created by the app are reserved and identified before native transfer. Cancellation/failure removes incomplete owned files and the native partfile, preserving verified completed files, unrelated files and pre-existing external files. History survives Cancel; removal waits for native disk acknowledgement. Persisted ownership/completion proofs protect completed files across restart. Explicit Delete payload remains separately confirmed.

Browser failures/cancellation remove owned incomplete native copies. Chrome is told to cancel incomplete transport downloads; a fully received browser source remains until native verification succeeds or the user handles it explicitly. The extension must be reloaded to apply changed bridge code. This is not permission to erase a complete received copy or arbitrary browser files.

Historical orphan files without matching ownership evidence are not bulk-deleted by filename or extension. Their presence does not imply a new task may overwrite or delete them.

## Verification scope

The implementation is built and checked as one completed change set, followed by targeted repairs if necessary. Tests use hidden windows, private databases and generated payloads. Process-level Qt scale factors exercise rendering at 100%, 125% and 150%; they do not change Windows display settings or establish multi-monitor/hardware DPI acceptance. Final results and remaining limitations are recorded in DEVELOPMENT_PROGRESS.md.

## Image capture repair

Images route to the configured base folder’s Pictures category using MIME type and common image suffixes. No image conversion is applied: the received bytes, original dimensions and format are preserved and independently verified during import.

The browser transport can resume an accepted Queued response before Chrome finalizes its routed filename. It does not adopt a provisional wrong path, and it still holds genuinely paused tasks and queued responses waiting for an available native copy slot. Completed responses can be imported from their actual final path.

The extension adds Save image to Downshift to image context menus. It starts the original browser request with saveAs disabled, so Downshift owns the destination dialog and final verification. Chrome remains the underlying transport where browser credentials or generated responses require it. Chrome’s own built-in Save image as command may still invoke its browser chooser; MV3 does not provide an API to replace that chooser. Use the Downshift image action to avoid it. Reload the extension after applying this update.
