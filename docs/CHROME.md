# Downshift for Chrome

The Windows EXE installer registers the native host and defaults to opening a Chrome setup guide, Chrome's Extensions page and the installed extension folder. Video tools install automatically during EXE setup. This unpublished local extension still requires Developer mode > Load unpacked once; automatic Windows external installation requires a Chrome Web Store listing and browser confirmation. Setup does not edit Chrome profiles or managed policies. See [Chrome's installation rules](https://developer.chrome.com/docs/extensions/how-to/distribute/install-extensions) and [packaging](PACKAGING.md).

## Magnet links, 2026-10-16

Reload the updated unpacked Downshift extension and refresh existing pages. With capture enabled, a normal magnet-link click opens the native Add torrent dialog with the link already filled in. Press Enter or Read metadata, choose files and confirm before payload downloading. This uses the existing restricted native host and local pipe; HTTP downloading remains unchanged. Capture-off and incognito are respected. This does not register a Windows-wide magnet handler or intercept address-bar protocol navigation.

## Capture history guard, 0.4.4

Completed Chrome download history is not a capture source. Unknown completed items from metadata changes or browser restoration are ignored. A tiny already-completed file needs a current filename event or a fresh creation event with a recent start time; saved handoff receipts remain recoverable. Bounded completed-ID receipts prevent delayed duplicate events from recapturing a delivered response across worker restarts. Reload the unpacked extension after updating the app. This fix does not bulk-delete prior cancelled rows or their files.

## Browser response bridge, 0.4.3

This supersedes the narrower 0.4.2 capture repair below. Use the updated app and reload the unpacked extension (capture protocol 4), then refresh existing pages. Blob/data, authenticated GET and POST downloads can now finish in Downshift from the original received response. No cookie/body export or URL-by-URL rules are required. Native destination selection, history and integrity checks apply; the browser is a transport for responses that cannot safely be refetched. Its normal download UI is suppressed while enabled and confirmed transport entries are cleaned after native completion. See [architecture, recovery, disk costs and verification scope](BROWSER_RESPONSE_CAPTURE.md). The previous statements that blob/authenticated downloads merely stay held describe the earlier implementation.

## Historical extension-only capture repair, 0.4.2

The earlier extension-only repair used capture protocol 3 (application/manifest version 0.4.2). The following records that repair's scope and tests; it does not describe current 0.4.3 behavior. The browser bridge above requires both the rebuilt app and reloaded extension.

Enabled capture rechecks downloads after Chrome Resume, relevant metadata changes, worker startup and reconciliation. Blocked rows are paused again before returning; old bypass IDs cannot exempt live downloads while enabled. Failures stay held and transient failures retry with bounded backoff. Explicitly switching capture off permits Chrome again. Chrome-approved verdicts remain required before native handoff; this is not a bypass of browser security checks.

Filename determination remains outstanding until native acceptance/cancellation or explicit capture-off, preventing tiny attachments finishing during preparation. Chrome's proposed filename is kept in bounded transient memory because search() can return an empty name at that stage. Long Windows names retain the extension within native limits. Direct-link detection accepts unlisted file suffixes instead of relying on a small allowlist; web page/handler suffixes and opaque URLs retain download-event capture. See Chrome's [filename event contract](https://developer.chrome.com/docs/extensions/reference/api/downloads#event-onDeterminingFilename).

The supplied public RobloxStudioInstaller resource was fully downloaded through the actual isolated Chrome/native interface and independently hashed, without creating a Chrome download. Complete tiny/redirect/custom-suffix fixtures also pass. Normal-history and torrent code are untouched. This source change is local and uncommitted at the user's request.

Browser-only blob URLs (including generated ChatGPT attachments) are not HTTPS resources and cannot be fetched by the current curl handoff. They are held with an explanation instead of silently completing through Chrome. A browser-to-native byte bridge is still absent. Authenticated resources can also be held when the native public request is refused. Do not interpret a held download as completed or promise all browser resources are transferable. Initial network bytes can arrive before extension events; policy restrictions, disabled extensions, competing interceptors and forced browser/worker termination remain outside a universal interception guarantee.

Phase 7 provides a Manifest V3 extension, a separate Windows Native Messaging host, and a staged handoff to the native queue. For the portable distribution run Setup.cmd and load its extension folder; see [portable setup](PACKAGING.md). The source-development setup is below. Chrome Web Store publication is not provided.

## Install locally

Build Release first, then open PowerShell in `C:\Users\HP\Desktop\Projects\Downshift` and run:

```powershell
./scripts/Register-ChromeHost.ps1
./scripts/Install-MediaTools.ps1
./build/windows-msvc/Release/Downshift.exe
```

In Chrome, open `chrome://extensions`, enable Developer mode, select **Load unpacked**, and choose `C:\Users\HP\Desktop\Projects\Downshift\extension`. The stable extension ID is `empnpdkfnffpcgnfgdndkbielplcoioe`. **Use Downshift for downloads** is enabled on fresh installs; an existing explicit off setting is preserved. The extension opens the registered app when a candidate download or video button needs it. Keep the app open during transfers. The popup's **Open Downshift** button launches the registered adjacent application or activates the existing window.

For an existing installation, press **Reload** on the Downshift extension card, allow the new site permissions if Chrome asks, and reload video tabs. Version 0.3.0 adds **Download this video** buttons to detected HTML video players, including YouTube, dynamically inserted players, matching HTTP/HTTPS frames, and open shadow roots. Clicking a button opens quality selection and download progress in Downshift. Use the desktop **Download video** action to paste a video page or media URL directly. See [video support and limitations](MEDIA.md).

Registration writes only this Windows account's 64-bit `HKCU\Software\Google\Chrome\NativeMessagingHosts\org.ushalabs.downshift` key and an adjacent host manifest. Administrator rights are unnecessary. For Debug use `./scripts/Register-ChromeHost.ps1 -Configuration Debug`. Re-register after moving the project/build. Remove the extension in Chrome and run `./scripts/Register-ChromeHost.ps1 -Unregister` to remove registration. Queue history/files are retained. Chrome's native-host manifest and origin restrictions follow its [Native Messaging documentation](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging).

## Supported downloads and capture

Public HTTP/HTTPS files can use either verified ranges or ordinary streaming. Default destinations follow video/audio/archive/other categories. Enabled capture promptly pauses Chrome, opens Downshift, prepares an inert record, verifies browser cancellation, and opens a per-download save-location window. Start download begins the native transfer. Unsupported, authenticated or unavailable handoffs stay paused while enabled; turn capture off explicitly to use Chrome. See [every-file behavior and limits](GENERAL_DOWNLOADS.md).

Chrome reports events after transfer begins: some initial bytes and downloads that finish before interception cannot be prevented universally. Incognito, unsafe Chrome verdicts and this extension's own recovery downloads are excluded. No automatic Chrome fallback is attempted while enabled. TLS verification and existing engine redirect/filename policies remain enabled.

Cookies, authorization headers, referrers, and browser credentials are never forwarded. Chrome exposes download metadata but does not supply the original response's ETag or a trusted checksum to this extension. Native preflight pins the public representation's strong ETag, resolved URL, and size; the engine rechecks those before downloading. A server serving a different public representation of the same length as a logged-in browser response cannot be identified from that metadata. Use Chrome for personalized resources; this integration does not promise authenticated download equivalence. Token-bearing download URLs are passed transiently to the host and encrypted in native persistence.

The extension uses `downloads`, `nativeMessaging`, `storage`, `alarms`, and nonblocking `webRequest`, with HTTP/HTTPS site access for player buttons and media response detection. It does not request cookie or browsing-history permissions. Media addresses are kept in bounded worker memory (128 tabs, 32 candidates per tab, ten-minute selection lifetime), removed on tab close, and lost on worker restart. Video URLs are sent only when a user clicks the button. Content scripts run in Chrome's isolated world; there is no page-message bridge and synthetic page clicks are ignored. Download events/control follow the [Chrome downloads API](https://developer.chrome.com/docs/extensions/reference/api/downloads). Download URLs are not copied into extension storage or diagnostic logs. Durable browser state contains download IDs, random handoff keys, decisions, and brief notices. Chrome itself retains its own download history.

## How a handoff recovers

The extension promptly pauses Chrome, opens the app, and asks the native worker to preflight the public resource. The queue atomically saves an encrypted **Awaiting Chrome** entry and receipt before acknowledging preparation. Prepared entries cannot start through normal desktop controls. Only then does the extension cancel Chrome, verify `USER_CANCELED`, and request commit. Commit atomically saves the entry as paused for destination selection and marks the receipt committed; Start download queues it. Repeated messages return the same receipt and never create a second job. The browser saves every decision stage so a replacement worker can reconcile with the durable native receipt. Chrome may stop and restart MV3 workers; see its [worker lifecycle](https://developer.chrome.com/docs/extensions/develop/concepts/service-workers/lifecycle).

Chrome cancellation and SQLite commit cannot be one atomic transaction. If the app disconnects before cancellation, enabled capture holds Chrome paused. If acknowledgement is lost after cancellation, the extension holds the decision pending until it can query the native receipt; immediately restarting could duplicate a committed native transfer. Reopen Downshift and press **Check connection / retry pending**. **Leave pending downloads in Chrome** disables future interception and requests a durable abort before resuming/restarting Chrome. A committed receipt stays managed in Downshift. Browser restart of a cancelled link is best effort; expired links or browser authentication rules may require retry from the original page. The desktop's **Cancel pending browser handoff** action aborts an uncommitted prepared entry. Enabled capture continues to hold Chrome; turn capture off in the extension to resume there.

SQLite schema 2 migrates schema 1 and retains receipts even after history removal, preserving idempotence. Receipt payloads and queue metadata use Windows user-scoped DPAPI. There is a 10,000-receipt bound; new handoffs then remain paused in Chrome while capture is enabled. The extension holds at most 20 pending decisions and 1,000 recent bypass IDs. Receipt pruning is not implemented. Capacity failures stay paused while enabled. Overflow IDs are bounded to 1,000 and resumed on explicit disable.

## Local process boundary

The host accepts only the fixed extension origin, validates command fields, and reads length-prefixed UTF-8 JSON bounded to 64 KiB. Its stdout contains protocol frames only. It is separate from the GUI. A current-user SID named pipe has a protected DACL for that account and SYSTEM, rejects remote clients, verifies both peers' process user, and refuses a competing first pipe instance. IPC/preflight and database work run off the GUI thread. Preflight has an 8-second transfer bound; IPC has a 12-second response bound and cancellable overlapped operations. This protects the Windows account boundary; software already running as the same user can impersonate a permitted local client. Foreign-user/remote rejection is enforced in code, not claimed as an executed multi-account penetration test. Downloads never execute automatically.

## Testing and competing browser integrations

`browser.native` tests the actual host/pipe/queue/engine. `browser.extension` tests the real controller with deterministic Chrome API substitutes for failure timing. A separate [actual Chrome test](TESTING.md#actual-chrome-test) loads the MV3 extension into an isolated Chrome for Testing profile and uses the real registry, host, Qt window, queue, and engine. Test registration backs up and restores any previous host value. Tests do not install the extension into your normal profile.

On the development machine IDM's native integration intercepted even an isolated Chrome download, returning `204 Intercepted by the IDM Advanced Integration` before Downshift could handle it. For isolated tests only, the identical official Chrome for Testing executable was copied to `DownshiftBrowserTest.exe`; original/copy SHA-256 matched. IDM settings and the user's browser profile were not changed. Other download-manager integrations can preempt normal Chrome downloads too; select the intended interceptor when using the extension. This is not an IDM performance comparison.

## 0.3.2 handoff repair

Trusted ordinary clicks on explicit download anchors or recognized HTTP/HTTPS file extensions are stopped before browser navigation and sent through native prepare/commit. Their destination window opens without starting a Chrome download. Modified clicks, unsupported URLs and unknown server-side attachments retain normal event capture. This cannot eliminate all Chrome download activity: server attachments, script-generated downloads and browser commands can start before the downloads API reports them. After native commit is confirmed, Downshift removes only its own cancelled handoff entry with downloads.erase; failed or uncertain commits retain their recovery state. An already displayed browser notification cannot be universally suppressed retroactively. Chrome's Manifest V3 blocking webRequest API is restricted to policy-installed extensions; this extension does not introduce enterprise installation requirements.

Direct handoff keys survive worker restart without persisting link URLs in extension storage. Native receipts prevent duplicate starts after a lost commit acknowledgement. URLs remain transient or encrypted in native history. Disabling capture restores ordinary link behavior; enabled failures never silently start a second Chrome download.

Reload Downshift at chrome://extensions, refresh existing pages, then open its popup and confirm Version 0.3.2. The normal Chrome profile is not reloaded or changed by development tests. Old content scripts can survive navigation/update, so the worker also checks Chrome's current top-level YouTube tab URL. Player IDs use the same exact eleven-character rule as the native validator; malformed player title URLs cannot override the valid watch page. Native rejection is returned to the page instead of acknowledging a successful video window.

API references: [download events, cancellation and erase](https://developer.chrome.com/docs/extensions/reference/api/downloads), [Manifest V3 webRequest restrictions](https://developer.chrome.com/docs/extensions/reference/api/webRequest).

## Running-version coordination and test isolation (0.3.3)

The popup detects an old worker or differing app/extension versions and offers Apply update. Apply it, restart the updated app when needed, and refresh existing pages. An absent worker response has a specific update message. Native prepare accepts an optional capability-gated server-filename choice; explicit download names are preserved.

New browser/native tests use org.ushalabs.downshift.test, a separate per-user test pipe, hidden test windows, private queues and a copied extension with only its host name redirected. They never replace the normal org.ushalabs.downshift registration. The test host cannot launch the user's real app when its fixture is unavailable. Production keeps its original host, secure pipe, origin restriction and singleton. Test executables are not redistributed in portable packages. Earlier shared-pipe test notes remain historical; only rebuilding the running product EXE requires closing an idle app.
