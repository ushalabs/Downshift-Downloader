# Engine verification

Run `ctest --preset release` and `ctest --preset debug` after their corresponding builds. The five groups cover dependency lifecycle, two Qt startup/rendering checks, engine units, and HTTP/HTTPS integration. Python 3.10+ is needed only for integration servers, not either native executable. Test output/timing are correctness evidence, not benchmarks.

## Native checks

`tests/EngineTests.cpp` verifies response isolation, 64-bit range parsing, invalid/ambiguous headers, bounded input, strong/weak validators, and filename handling. Scheduler checks establish complete nonoverlapping coverage, including totals above 4 GB.

File tests perform out-of-order writes, verify known SHA-256 output, reject incorrect checksums/sizes, preserve existing partials/destinations, simulate a destination appearing before rename, reject invalid names/offsets, and truncate stale bytes for fallback. A real sparse NTFS file is written and read above offset 4 GB, then finalized at the correct 64-bit length. Sparse allocation avoids consuming 4 GB of disk; production segmented downloads reserve actual disk space.

## Controlled network checks

`tests/IntegrationTests.py` binds loopback with OS-assigned ports. Deterministic small and 17 MiB payloads are checked byte-for-byte and by SHA-256. Five fixed chunks require reassignment after the initial four concurrent connections. Request records check identity encoding, conditional requests, nonoverlapping coverage, and connection limits.

Scenarios cover advertised/unadvertised ranges; ignored ranges; missing/weak ETags; malformed probes; known/unknown lengths; chunked framing/trailers; interim responses; redirects; empty/one-byte files; fallback after a range transmits data; incorrect ranges/totals; version changes; short/oversized bodies; conditional errors; HTTP 401/403/404/429/500/503; encodings/ambiguous lengths; disconnects; timeouts; wrong/invalid checksums; unsafe schemes/credentials; existing files; and worker cancellation.

HTTPS exercises successful segmented/streaming requests with explicit test trust, a restricted loopback CONNECT proxy, untrusted-certificate rejection, hostname mismatch, and prohibited downgrade. Ambient proxy variables are excluded from fixture requests. The fixture uses a self-signed test root as its loopback server certificate. Its public disposable key/certificate are documented in `tests/fixtures/README.md`. Verification is never disabled and no certificate is added to Windows' trust store. Certificates expire in October 2036.

The fixture serves HTTP/1.1, including TLS. HTTP/2 capability is checked by the foundation test and requested by the engine when ALPN permits, but HTTP/2 wire behavior is not exercised here. HTTP/3, public-network throughput, IDM comparisons, retry/resume, crash recovery, disk-full behavior, and GUI downloads are not claimed as verified. Actual results are in [development progress](DEVELOPMENT_PROGRESS.md).
