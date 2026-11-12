# Browser extensions on Windows

Downshift has extension builds for Chrome, Microsoft Edge, Mozilla Firefox and
Opera. Install the updated Windows application to register the native bridge.
The ordinary Windows setup registers Chrome/Opera, Edge and Firefox together.

In Downshift, open Settings, Integration, then Set up browser extension. Choose
the browser. Downshift opens its extension page and copies the required path.

For Edge and Opera, enable Developer mode, choose Load unpacked and paste the
copied folder path. The standalone ZIP files also contain unpacked folders and
an INSTALL.txt guide.

Firefox requires version 140 or newer. The local unsigned build can be loaded
through about:debugging, This Firefox, Load Temporary Add-on. Paste the copied
manifest.json path. Firefox removes temporary add-ons when it closes. Permanent
installation requires Mozilla signing. Private windows are excluded from this
Firefox build, and Firefox retains control of its download-panel interface.

Firefox's installation consent covers the local companion connection. Download
links, selected content metadata and download actions go to the Downshift app on
the same computer. See the extension's PRIVACY.md for the full disclosure.

Leave Use Downshift for downloads enabled. Normal file downloads and the Download
this video overlay use the existing native download and media pipelines. Browser
responses are retained for generated files, POST responses and signed-in files,
and are removed only after Downshift verifies the completed destination.

Build the three additional browser packages with PowerShell from the project root.

```powershell
./scripts/Build-BrowserExtensions.ps1 -OutputDirectory ./dist/browser-extensions
```

Each browser package retains the same shared source. The Firefox manifest uses a
module background page and a fixed add-on ID. Chromium builds retain the existing
extension key and use a module service worker. Store publication requires each
browser's normal submission process and the corresponding final extension ID.
