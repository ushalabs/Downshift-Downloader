# Code signing policy

## Current status

The free SignPath Foundation application was declined. Downshift is not signed
by SignPath Foundation and does not currently have publicly trusted code signing.
The manual NSIS installer is unsigned; Windows may show a SmartScreen warning.
The current manual MSIX uses a local UshaLabs certificate and requires explicit
certificate trust on each computer, as explained in the release instructions.

The previous application proposed this attribution, which does not describe
the current unsigned distribution:

Free code signing provided by [SignPath.io](https://about.signpath.io), certificate
by [SignPath Foundation](https://signpath.org).

SignPath Foundation would be the certificate publisher. UshaLabs remains the
developer and maintainer of Downshift. A managed signing service retains the
private signing keys. They must never be stored in this repository or release
archives.

## Maintainer responsibilities

- Author, committer, and reviewer: [usha labs / ushalabs](https://github.com/ushalabs).
- Release and signing approver: [usha labs / ushalabs](https://github.com/ushalabs).
- External contributions require maintainer review before inclusion in a release.
- Project members must use multi-factor authentication for GitHub and SignPath.
- Every release signing request requires approval by the designated maintainer.

## Release provenance

Signing requests must identify the exact source commit, build workflow, and
artifacts. Submitted application binaries must be built from the project's public
source through the approved build pipeline. Dependency DLLs and downloaded media
tools retain their upstream provenance and must not be represented as binaries
authored by Downshift.

Release packages include license notices and matching application and dependency
source archives. Package checks, signature verification, and SHA-256 checksums
remain part of release validation. A successful signature confirms provenance;
download throughput and website compatibility depend on their respective sources.

## Privacy

See the [Downshift privacy policy](../extension/PRIVACY.md). Download history stays
in the local encrypted store. The application contacts user-selected download
sources and the documented upstream services needed to install video tools.
Browser cookies, saved passwords, and authorization headers stay in the browser.
The extension includes no remote analytics or advertising service.

The signing service processes project artifacts and maintainer account details
under its [privacy policy](https://signpath.io/privacy-policy). Users installing
Downshift do not need a signing-service account.
