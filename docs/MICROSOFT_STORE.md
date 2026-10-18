# Publishing Downshift on Microsoft Store

This is a preparation guide, not a completed Store submission. The current local setup is unsigned and downloads the video tools during installation. It is suitable for local preview testing; it does not yet satisfy Microsoft's EXE/MSI Store package requirements.

## Choose a packaging route

**Keep the EXE installer:** this preserves the existing per-user native-host integration. Microsoft requires an offline, standalone installer, a permanent versioned HTTPS download URL whose bytes never change after submission, silent installation, and trusted Authenticode signatures on the installer and its PE payload. NSIS already supports the case-sensitive `/S` silent switch. The present online video bootstrap must be replaced with an offline payload, with complete corresponding sources/licenses and the required binary signatures, before this route can be submitted. UshaLabs in the welcome page or publisher metadata is developer attribution, not a signing certificate.

**Create an MSIX package:** Microsoft recommends MSIX and supplies Store signing and hosting. This requires a dedicated packaging and compatibility pass for Downshift's Chrome native-host registration, writable history/helper/staging paths, upgrades, tray lifetime, protocol handling and video tools. It is not a matter of renaming the EXE to MSIX. Microsoft Store installation also does not install or approve the separate Chrome extension; keep the included Chrome instructions until a separate Chrome Web Store distribution is prepared.

Sources: [EXE/MSI package requirements](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-package-requirements), [Microsoft's publishing overview and MSIX option](https://learn.microsoft.com/en-us/windows/apps/publish/get-started), [Chrome extension installation rules](https://developer.chrome.com/docs/extensions/how-to/distribute/install-extensions).

## Submission steps

1. Register and verify a Windows developer account in [Partner Center](https://partner.microsoft.com/dashboard). Choose the account type that matches your actual use. Microsoft's current guidance puts business/professional distribution, including freelancers, under Company accounts; hobby/noncommercial use belongs under Individual. Set up the publisher identity you want to use for UshaLabs.
2. In Apps and games, choose New product, select the intended EXE/MSI or MSIX route, check whether Downshift is available, and reserve the product name.
3. Prepare and test the Store-ready package for that route. Validate a fresh standard-user installation, offline dependencies, Chrome instructions, real downloads, pause/restart, upgrades and uninstall on a clean Windows machine. Keep exact sources and required dependency notices available with distribution.
4. Complete the Store draft: availability/pricing, category and system requirements, IARC age-rating questionnaire, packages, listing text, screenshots/icons, support contact and privacy policy where required. For EXE submission provide the immutable HTTPS installer URL and `/S` install parameter. Explain video helpers, torrent functionality and manual Chrome setup in the certification notes so reviewers can exercise the features accurately.
5. Submit for certification after the package and listing are ready. Review failures in Partner Center, correct them and submit again. Each EXE update needs a new versioned URL and a new submission; do not replace an already submitted binary at its original URL.

Sources: [Get started and create a Store submission](https://learn.microsoft.com/en-us/windows/apps/publish/get-started), [EXE/MSI package upload fields](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/upload-app-packages), [EXE/MSI certification](https://learn.microsoft.com/en-us/windows/apps/publish/publish-your-app/msi/app-certification-process).
