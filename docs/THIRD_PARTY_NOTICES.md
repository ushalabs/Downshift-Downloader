# Downshift third-party licensing

Downshift's original application source is licensed under MIT. Dependency licenses are separate; the MIT license does not relicense Qt or other bundled code.

## Current direct dependencies

| Dependency | License / basis | Source |
|---|---|---|
| Qt Core, Gui, Widgets | Open-source LGPLv3 option; link dynamically | [Qt licensing](https://doc.qt.io/qt-6/licensing.html) |
| libcurl | curl license, with ISC/BSD notices for included components | [curl license](https://curl.se/docs/copyright.html) |
| SQLite | Public domain; upstream blessing | [SQLite copyright](https://sqlite.org/copyright.html) |

The selected Qt module set permits the LGPL option, so the original application can use MIT while complying with the libraries' terms. No GPL-only optional Qt modules are intentionally selected. See [Qt LGPL obligations](https://www.qt.io/development/open-source-lgpl-obligations).

## Portable binary distribution

The portable archive includes the actual installed vcpkg `share/<port>/copyright` notices and SPDX resource inventories under `licenses`, including Qt's LGPLv3 and GPLv3 texts and third-party notices. Qt Core/Gui/Widgets and their Windows platform plugin are dynamically linked. Downshift Settings prominently identifies Qt and links this notice. Library replacement and reverse engineering for debugging modifications are permitted; see [rebuild instructions](REBUILD_LIBRARIES.md). Package hashes identify the published files and are not checked by the application to forbid modified libraries.

The separate dependency-source ZIP provided beside each release binary includes exact SHA-512-verified upstream sources for Qt, curl, SQLite, zlib, nghttp2, md4c, PCRE2 (including sljit), double-conversion and Khronos header registries. The pinned vcpkg source archive includes all corresponding patches, portfiles and build scripts. Application source is supplied through the matching public Git tag. This completes the selected source/notice delivery route; preserve these accompanying files when redistributing.

App-local Microsoft Visual C++ 2022 x64 runtime DLLs come from the licensed Build Tools redistributable directory. Their Microsoft terms are separate from MIT. They are used to run the application on Windows, not as development tools. See Microsoft's [redistribution requirements](https://learn.microsoft.com/en-us/cpp/windows/redistributing-visual-cpp-files?view=msvc-170) and the applicable [Visual Studio terms](https://visualstudio.microsoft.com/license-terms/). Windows system libraries are provided by the operating system.

Optional yt-dlp, Deno and FFmpeg executables are **not** included in the binary archive. Enable-video.cmd downloads immutable upstream releases with pinned SHA-256 verification; their upstream licenses apply (the packaged yt-dlp executable uses GPLv3+, Deno MIT, the chosen FFmpeg build LGPL). Downshift original code remains MIT. Sources/license information: [yt-dlp](https://github.com/yt-dlp/yt-dlp), [Deno](https://github.com/denoland/deno), [FFmpeg builds](https://github.com/BtbN/FFmpeg-Builds). The setup script records actual asset URLs/hashes in media-tools/sources.json and includes the FFmpeg license. Video setup requires Internet access; ordinary native file downloading needs no helper tools or developer runtime.

No proprietary IDM source, artwork, or implementation is included.
