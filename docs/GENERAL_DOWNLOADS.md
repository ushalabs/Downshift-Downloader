# Every file in Downshift

Downshift 0.3.0 manages ordinary HTTP/HTTPS files with its native engine and supported videos with the optional media tools. It keeps both kinds in encrypted local history. It never automatically runs downloaded files.

## Default folders

The Windows Downloads folder is the initial base. Settings can change that base. Categories are identified from the filename and, for browser handoffs, the server's media type; this is not a full-content decoder.

| Content | Folder under the base |
|---|---|
| Video | Videos |
| Audio | Audios |
| Compressed archives including ZIP, RAR, 7z and tar | Zip |
| Executables and all other files, including documents | Programs |

Add download, Chrome's Save download window, and the video window all allow a different destination for that individual download. Changing one destination does not change other downloads. Existing history and files are not relocated. Media retries retain their chosen folder. A browser handoff is saved as paused until Start download is pressed; Cancel leaves its record for later Resume or removal.

## Chrome capture

Capture starts enabled on a fresh extension install; existing off preferences persist. An eligible Chrome download is paused promptly, the registered Downshift app is opened, and the native worker checks the public file. After verified browser cancellation, the app shows the destination window. Both range downloads and ordinary streaming servers are supported. Unsupported or unavailable handoffs remain paused while capture is on: the popup offers retry or turning capture off to use Chrome. There is no silent Chrome fallback in enabled capture.

Chrome's [downloads API](https://developer.chrome.com/docs/extensions/reference/api/downloads) reports events after a download begins. Initial browser traffic and files completed before interception cannot be universally prevented. Incognito, Chrome-blocked dangerous downloads, inaccessible authenticated resources, and browser-only URLs cannot be treated as ordinary public HTTP files. Cookies and credentials are not forwarded. Blob video playback can be resolved through supported page/media extraction when available; arbitrary blob files cannot. DRM, protected/private streams and unsupported sites are not universally downloadable. Turn capture off for such resources. A lost acknowledgement after browser cancellation is reconciled against the native receipt before restarting anything, avoiding duplicates.

## Desktop

The default window is 860 × 570 logical pixels with a pink light theme and plum dark theme. It can be resized or maximized. At narrow widths the main list shows filename, size, progress, speed and status; a wider window also shows downloaded bytes, time left, connections and priority. The selected download's details remain available below the list. Windows display scaling changes the physical pixel size.

Completed video status requires a real nonempty saved file with a video stream and positive duration. It is metadata verification, not full frame decoding. Open folder uses the saved path. Previous unrecorded video attempts cannot be reconstructed automatically.

## Pause, resume and transfer troubleshooting in 0.3.2

Explicit Pause now saves hash-verified prefixes from accepted, strongly validated range responses, including a range that has not finished. Resume rehashes saved bytes and rechecks the remote version before requesting missing ranges. Failed, malformed or unvalidated response prefixes are excluded. A pre-0.3.2 pause with no checkpoint cannot be reconstructed safely: Resume reports the missing checkpoint and requires an explicit Restart, preserving the existing partial until that action. Pause/resume still requires stable range support and a strong ETag; non-resumable servers cannot provide safe byte continuation.

Automatic transfers between 1 and 32 MiB can initially use up to eight connections, bounded by the configured maximum; tiny files use one and large files initially use four, with the existing adaptive controller. There is no default speed cap. On eligible transport failures, bounded retries use fresh HTTP/1.1 connections rather than repeatedly reusing a broken or stalled negotiated connection. TLS, exact ranges, strong validators, retry limits and destination preservation remain enforced. Progress can update inside paced receive callbacks, so the desktop can observe and pause a transfer while libcurl drains socket buffers.

The user's WinRAR record showed 320 KiB written but zero checkpointed bytes and no speed cap. An independent Windows curl range request to the same public source delivered about 20,940 bytes/s, so the observed 30–40 KiB/s does not establish a Downshift-only bottleneck or the ISP's capacity. Preliminary two/eight-connection runs had different outcomes and overlapped other Internet traffic; they are diagnostic observations, not a comparable performance benchmark. No constant 3 MB/s or speed superiority is promised.
