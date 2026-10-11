# Downshift third-party licensing

Downshift's original application source is licensed under MIT. Dependency licenses are separate; the MIT license does not relicense Qt or other bundled code.

## Current direct dependencies

| Dependency | License / basis | Source |
|---|---|---|
| Qt Core, Gui, Widgets | Open-source LGPLv3 option; link dynamically | [Qt licensing](https://doc.qt.io/qt-6/licensing.html) |
| libcurl | curl license, with ISC/BSD notices for included components | [curl license](https://curl.se/docs/copyright.html) |
| SQLite | Public domain; upstream blessing | [SQLite copyright](https://sqlite.org/copyright.html) |

The selected Qt module set permits the LGPL option, so the original application can use MIT while complying with the libraries' terms. No GPL-only optional Qt modules are intentionally selected. See [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations).

## Distribution requirements

Before distributing binaries, collect the actual vcpkg package `share/<port>/copyright` notices for every shipped dependency, include Qt's LGPLv3 and GPLv3 texts and applicable third-party notices, and provide the corresponding library source/source availability information required by those licenses. Preserve users' ability to replace/rebuild LGPL libraries and document that process. Dynamic linking alone does not complete these obligations.

Transitive dependencies include components such as zlib, nghttp2, md4c, PCRE2, and double-conversion; the exact built/shipped inventory must be audited during packaging. This file records the source-development licensing decision and the pending distribution work. It is not a completed binary-distribution notice bundle.

No proprietary IDM source, artwork, or implementation is included.
