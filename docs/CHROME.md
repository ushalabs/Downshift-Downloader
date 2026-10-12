# Downshift for Chrome

Phase 7 provides a Manifest V3 extension, a separate Windows Native Messaging host, and a staged handoff to the native queue. This is a local development installation; an installer and Chrome Web Store publication are later work.

## Install locally

Build Release first, then open PowerShell in `C:\Users\HP\Desktop\Projects\Downshift` and run:

```powershell
./scripts/Register-ChromeHost.ps1
./build/windows-msvc/Release/Downshift.exe
```

In Chrome, open `chrome://extensions`, enable Developer mode, select **Load unpacked**, and choose `C:\Users\HP\Desktop\Projects\Downshift\extension`. The stable extension ID is `empnpdkfnffpcgnfgdndkbielplcoioe`. Open its popup and enable **Send supported downloads to Downshift**. Interception is off by default. Keep Downshift open; closing the desktop app pauses its workers and makes automatic handoff unavailable. The popup's **Open Downshift** button launches the registered adjacent application or activates the existing window.

Registration writes only this Windows account's 64-bit `HKCU\Software\Google\Chrome\NativeMessagingHosts\org.ushalabs.downshift` key and an adjacent host manifest. Administrator rights are unnecessary. For Debug use `./scripts/Register-ChromeHost.ps1 -Configuration Debug`. Re-register after moving the project/build. Remove the extension in Chrome and run `./scripts/Register-ChromeHost.ps1 -Unregister` to remove registration. Queue history/files are retained. Chrome's native-host manifest and origin restrictions follow its [Native Messaging documentation](https://developer.chrome.com/docs/extensions/develop/concepts/native-messaging).

## Supported downloads and fallback

Automatic handoff supports public HTTP/HTTPS files of at least 256 KiB with a valid actual byte-range response, a strong ETag, untransformed content, and matching known Chrome size. Incognito, HTML, executable/script/shortcut filenames, unsafe Chrome download verdicts, this extension's own fallback downloads, and small files are excluded. Native preflight rejects authentication failures, unsupported streaming/validators, and different lengths; these downloads remain in Chrome. TLS verification and existing engine redirect/filename policies remain enabled.

Cookies, authorization headers, referrers, and browser credentials are never forwarded. Chrome exposes download metadata but does not supply the original response's ETag or a trusted checksum to this extension. Native preflight pins the public representation's strong ETag, resolved URL, and size; the engine rechecks those before downloading. A server serving a different public representation of the same length as a logged-in browser response cannot be identified from that metadata. Use Chrome for personalized resources; this integration does not promise authenticated download equivalence. Token-bearing download URLs are passed transiently to the host and encrypted in native persistence.

The extension uses only `downloads`, `nativeMessaging`, `storage`, and `alarms`. It has no site host permissions, content scripts, cookie access, or browsing-history access. Download events/control follow the [Chrome downloads API](https://developer.chrome.com/docs/extensions/reference/api/downloads). Download URLs are not copied into extension storage or diagnostic logs. Durable browser state contains download IDs, random handoff keys, decisions, and brief notices. Chrome itself retains its own download history.

## How a handoff recovers

The extension first checks the app, pauses Chrome, and asks the native worker to preflight the public resource. The queue atomically saves an encrypted **Awaiting Chrome** entry and receipt before acknowledging preparation. Prepared entries cannot start through normal desktop controls. Only then does the extension cancel Chrome, verify `USER_CANCELED`, and request commit. Commit atomically queues that entry and marks the receipt committed. Repeated messages return the same receipt and never create a second job. The browser saves every decision stage so a replacement worker can reconcile with the durable native receipt. Chrome may stop and restart MV3 workers; see its [worker lifecycle](https://developer.chrome.com/docs/extensions/develop/concepts/service-workers/lifecycle).

Chrome cancellation and SQLite commit cannot be one atomic transaction. If the app disconnects before cancellation, Chrome resumes where possible. If acknowledgement is lost after cancellation, the extension holds the decision pending until it can query the native receipt; immediately restarting could duplicate a committed native transfer. Reopen Downshift and press **Check connection / retry pending**. **Leave pending downloads in Chrome** disables future interception and requests a durable abort before resuming/restarting Chrome. A committed receipt stays managed in Downshift. Browser restart of a cancelled link is best effort; expired links or browser authentication rules may require retry from the original page. The desktop's **Leave this download in Chrome** action aborts an uncommitted prepared entry; browser reconciliation then restores it where possible.

SQLite schema 2 migrates schema 1 and retains receipts even after history removal, preserving idempotence. Receipt payloads and queue metadata use Windows user-scoped DPAPI. There is a 10,000-receipt bound; new handoffs then remain in Chrome. The extension holds at most 20 pending decisions and 1,000 recent bypass IDs. Receipt pruning is not implemented.

## Local process boundary

The host accepts only the fixed extension origin, validates command fields, and reads length-prefixed UTF-8 JSON bounded to 64 KiB. Its stdout contains protocol frames only. It is separate from the GUI. A current-user SID named pipe has a protected DACL for that account and SYSTEM, rejects remote clients, verifies both peers' process user, and refuses a competing first pipe instance. IPC/preflight and database work run off the GUI thread. Preflight has an 8-second transfer bound; IPC has a 12-second response bound and cancellable overlapped operations. This protects the Windows account boundary; software already running as the same user can impersonate a permitted local client. Foreign-user/remote rejection is enforced in code, not claimed as an executed multi-account penetration test. Downloads never execute automatically.

## Testing and competing browser integrations

`browser.native` tests the actual host/pipe/queue/engine. `browser.extension` tests the real controller with deterministic Chrome API substitutes for failure timing. A separate [actual Chrome test](TESTING.md#actual-chrome-test) loads the MV3 extension into an isolated Chrome for Testing profile and uses the real registry, host, Qt window, queue, and engine. Test registration backs up and restores any previous host value. Tests do not install the extension into your normal profile.

On the development machine IDM's native integration intercepted even an isolated Chrome download, returning `204 Intercepted by the IDM Advanced Integration` before Downshift could handle it. For isolated tests only, the identical official Chrome for Testing executable was copied to `DownshiftBrowserTest.exe`; original/copy SHA-256 matched. IDM settings and the user's browser profile were not changed. Other download-manager integrations can preempt normal Chrome downloads too; select the intended interceptor when using the extension. This is not an IDM performance comparison.
