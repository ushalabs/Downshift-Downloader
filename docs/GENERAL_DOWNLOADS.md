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
