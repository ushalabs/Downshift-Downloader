# Windows portable distribution

Phase 9 uses the accepted portable-distribution route. Windows 10/11 x64 is required. Extract the complete ZIP to a permanent writable folder; double-click Setup.cmd, then the Desktop Downshift shortcut. The application runs without Visual Studio, CMake, Python, Node or a system Qt install. Microsoft VC143 runtime DLLs, shared Qt/curl/SQLite dependencies and the Windows Qt plugin are app-local. Keep these files beside the executable.

Setup is per-user, requires no administrator rights, registers the restricted Chrome Native Messaging host and creates a Desktop shortcut. Open chrome://extensions, enable Developer mode, Load unpacked, and select the package's extension folder. Chrome Web Store installation is not provided. Existing extension installations require Reload and reloading video pages. Source-development setup remains documented separately in INSTALLATION.md.

Enable-video.cmd optionally fetches the exact upstream yt-dlp, Deno and FFmpeg releases after SHA-256 verification. It needs Internet access and installs helpers in media-tools beside the app; the portable ZIP does not redistribute those executables. Ordinary file downloads run immediately without them. There is no automatic helper update.

Uninstall.cmd restores the saved prior native-host registration and Desktop shortcut only if this package still owns them. It leaves later installations alone and never deletes user downloads or encrypted history. Remove the extension in Chrome; delete the extracted app folder manually afterward. To move an installation, uninstall its integration in the original location first and run Setup in the new one. Local history remains under the Windows user application-data directory across package replacement.

This preview is unsigned; no Windows installer, store certification or auto-updater is claimed. Keep source and dependency-license bundles with redistribution. See THIRD_PARTY_NOTICES.md and REBUILD_LIBRARIES.md.

## Build and verification

From a verified Release source build:

```powershell
./scripts/Build-Portable.ps1 -RequireClean
./scripts/Test-Portable.ps1 -PackagePath ./build/releases/Downshift-0.4.4-windows-x64.zip -ReportPath ./build/package-test-report.json -TorrentTestDriver ./build/windows-msvc/Release/downshift_torrent_acceptance_tests.exe -PythonExecutable (Get-Command python).Source
```

The build script bundles only the two product executables and release runtime libraries, not tests/debug/developer tools or video helpers. It generates the extension ZIP, package manifest with SHA-256 per file, separate complete dependency-source kit with vcpkg patches/recipes, and SHA256SUMS.txt. Missing source archives are fetched from their recorded upstream location and must match the exact installed SPDX SHA-512. Release publication requires a clean checkout; development packages explicitly report dirty source.

The test extracts to a path containing spaces, verifies every manifest hash, requires app-local runtimes/licenses, checks extension identity/version, repeats setup, validates host path/origin and shortcut, then opens the actual packaged Qt window with PATH restricted to Windows system directories. It verifies the loaded Qt/curl/SQLite/VC runtime modules came from that extracted package. Smoke mode uses an isolated database and leaves the normal pipe/history untouched. Uninstall must restore prior registration/shortcut, retain the app files, and preserve a different later installation. Both successful and failed tests restore the original registry state and clean only their validated temporary directory.

Local acceptance: 13 checks passed using built-in Windows PowerShell 5.1; the package loaded its own runtime libraries. This is the development Windows 11 machine with a sanitized PATH, not a separately provisioned clean Windows VM. Debug desktop/startup regression checks also passed after the prominent Qt license notice was added. Final release evidence and remote Windows CI are recorded separately in DEVELOPMENT_PROGRESS.md.

## T4 local previews and source identity

When Git is intentionally left to the user, use `Build-Portable.ps1 -LocalPreview -OutputDirectory ./build/t4-local-preview`. The package is marked LOCAL-PREVIEW and nondistributable; `-RequireClean` continues to reject a dirty checkout even when LocalPreview is supplied. Both clean and preview packages include an application-source ZIP built from the actual tracked/nonignored source bytes, plus its hash and per-file source inventory. Compiled input fingerprints must match that source inventory. Private data/build/dependency work folders are excluded.

The extracted verification now checks matching application source, Rasterbar/OpenSSL runtime loading and notices, and runs the mixed eight-torrent/HTTP/HTTPS native Qt fixture using a verification-only driver beside the extracted DLLs. PNG/JPEG/WebP decoding and the native TLS backend are exercised there. Installer tests use only `org.ushalabs.downshift.packaging-test`; ordinary Setup/Uninstall retain their original namespace. Test fixtures and the test driver are not distributed. CI supplies the driver and Python path explicitly. Remote CI and a clean published artifact remain separate evidence from an uncommitted local preview.
