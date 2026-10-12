# Phase 8 performance

Phase 8 compares native Release clients on this Windows laptop. The final matrix covers decimal 100 MB, 1 GB, and 5 GB files, fixed 1/4/8/16 slots, automatic mode, and streaming. These are controlled HTTP/1.1 loopback observations with OS caches enabled; the fixture shares the machine. They do not measure Internet speed, cold physical storage, or IDM throughput.

## Measured bottlenecks and final changes

The [initial Phase 7 diagnostic](../benchmarks/results/phase8-initial-20261012.md) contains 36 verified samples. For 1 GB automatic downloads, about 1.44 seconds of checksum work, 1.0–1.6 seconds of cached write calls, and 0.5–0.7 seconds of flush work contributed to approximately 4.2 seconds of process wall time. This motivated fewer small I/O operations while retaining independent checksum rereads and durable checkpoint boundaries.

- Segmented responses combine validated, paced callbacks into bounded 64 KiB write buffers. Successful response verification flushes the short tail. Failed/cancelled uncheckpointed tails are discarded; retry progress subtracts actual written bytes, preventing underflow before a buffer fills.
- Segmented handles request a 256 KiB libcurl receive buffer. Streaming and probes retain the established default receive path; streaming writes validated callbacks directly. The pinned libcurl shares a receive buffer across one multi handle and still bounds individual callbacks. See [receive buffers](https://curl.se/libcurl/c/CURLOPT_BUFFERSIZE.html) and [callback bounds](https://curl.se/libcurl/c/CURLOPT_WRITEFUNCTION.html).
- FileWriter reuses a lazy 1 MiB heap buffer for checksum reads instead of 64 KiB stack reads. Completed-range hashes and final trusted SHA-256 still reread the file independently. Empty files allocate no checksum buffer.
- JSON telemetry records actual successful write/read calls, checksum-read bytes, and successful encrypted checkpoint count/time. Existing checkpoint boundaries and flush-before-journal ordering remain intact.
- Recovery metadata replacement retries only Windows access-denied, sharing-violation, or lock-violation errors: nine attempts with eight 25 ms waits, 200 ms of requested waits; filesystem calls and scheduling can add time. The previous journal remains authoritative until the flushed temporary file replaces it. Permanent errors still fail with the Windows error code; permissions, attributes, encryption, and validation are unchanged. See [recovery](RECOVERY.md).

Additional buffer capacity per active worker is bounded by one 1 MiB checksum buffer, up to sixteen 64 KiB segment buffers, and the requested receive buffer. Streaming uses only the additional checksum allocation. Written-byte progress can lag by less than 64 KiB per active segment. Pacing still sees every validated callback before buffering. TLS, response/length/range validation, ETag pinning, exclusive destinations, independent hashes, and queue limits remain enforced.

## Rejected prototypes and reliability diagnosis

The [first paired pilot](../benchmarks/results/phase8-pilot-20261012.md), 36 samples, applied write buffering to streaming too. Its 1 GB automatic paired median improved 10.8%, but streaming regressed 23.6%. A [second pilot](../benchmarks/results/phase8-receive-pilot-20261012.md), 18 samples, also enlarged streaming receive buffers and showed improvements at that time. Both exact variants are in [the pilot archive](../benchmarks/results/phase8-pilot-source.json); neither is the final implementation.

The [first full prototype attempt](../benchmarks/results/phase8-interrupted-20261012.md) stopped at 137 accepted samples when the original Phase 7 baseline could not replace recovery metadata. The [later large-file prototype](../benchmarks/results/phase8-large-files-20261012.md) stopped at 39 accepted samples when the optimized prototype encountered the same operation failure. These reports remain incomplete, and their samples are excluded from final acceptance. The later run also reproduced a 43.95% median paired streaming regression at 1 GB. That receive/write treatment was rejected, and the final streaming path restores Phase 7 behavior. Exact measured bytes remain in the [interrupted archive](../benchmarks/results/phase8-interrupted-source.json) and [wide-receive archive](../benchmarks/results/phase8-wide-receive-source.json).

An isolated Phase 7 diagnostic reproduced Windows error 5; [operation-specific evidence](evidence/phase8-journal-stage-diagnostic.json) identified metadata replacement. Its [instrumented source](evidence/phase8-journal-stage-source.json) is retained. No external blocking cause was established. Disk space was adequate. Production now uses the bounded replacement retry above, verified with real transient and persistent Windows reader locks. Persistent contention preserves the previous encrypted journal and removes temporary files.

To isolate I/O optimization from this reliability fix, the final comparison baseline is **Phase 7 engine with the identical Phase 8 journal replacement retry**. Only ResumeJournal.cpp differs from the original archived Phase 7 measured sources. Its body/network/checksum paths remain Phase 7. [The matched baseline archive](../benchmarks/results/phase8-matched-baseline-source.json) preserves exact source bytes, executable hash, toolchain, and this qualification. This is not a claim to have completed the matrix using the untouched Phase 7 executable.

The machine reported 998 MHz before the earlier pilot/session and 2995 MHz before a later session (WMI maximum 2995 MHz). No cause was established or setting changed. Reports capture one pre-run snapshot, not continuous frequency. Alternating pairs reduce but do not eliminate cache/CPU/background variation. A [restored-stream check](../benchmarks/results/phase8-restored-stream-20261013.md) passed six independent file verifications but still had variable paired speeds; the full matrix supplies final observations.

## Reproduce

Build Phase 7 checkpoint `017390e3167f0f6dad40f2671ae29f8f894d456a` in a separate source directory using the pinned MSVC/SDK/vcpkg dependencies. Replace only its ResumeJournal.cpp with the final version, then build its Release client. Preserve accurately fingerprinted source bytes and executable metadata in the same format as the matched archive. A rebuilt binary may have a different hash; supply its own accurate archive rather than relabeling existing evidence.

Build/test the current client, then run from the repository root with Python 3.10+:

```powershell
./scripts/Bootstrap.ps1
cmake --build --preset release --parallel 2
ctest --preset release --output-on-failure
python scripts/BenchmarkPhase8.py --baseline build/phase8-diagnostic-build/Release/DownshiftDownload.exe --baseline-source benchmarks/results/phase8-matched-baseline-source.json --output benchmarks/results/my-paired-run
```

The example baseline path is this local comparison build. Supply your own path/archive when rebuilding elsewhere. For a shorter run, add `--sizes 1GB --profiles unlimited --modes 4 auto stream`. Do not run builds, correctness suites, or another benchmark during timed samples.

The default matrix has 180 samples, 90 pairs, three repetitions per case. All six modes run for 100 MB under unlimited, per-connection 4 MiB/s, and shared 16 MiB/s profiles. Shaped profiles add 25 ms header delay. Large files use unlimited traffic by default; `--shape-large-files` extends shaping. Seed 20261012 shuffles modes and pair order. Fixed modes use 4 MiB chunks; automatic uses the unchanged adaptive policy in both clients, so mode comparisons include policy effects.

Each native process receives the same trusted full SHA-256. Independent Python rereads verify size/hash, contiguous nonoverlapping server coverage, payload totals, and hard concurrency limits before acceptance. Every sample has a distinct destination. Pending journal cleanup is rejected; failed attempts are recorded explicitly and stop the run. Existing reports are never overwritten. Raw JSON/CSV preserve order and source/executable/toolchain fingerprints. Markdown reports medians and median paired percentage changes, which differ from percentage changes between group medians. Interrupted reports explicitly remain incomplete.

## Final observations

Final acceptance: **180 independently verified samples, 90 pairs, 30 cases, three pairs per case; zero failed attempts.** No prototype samples are included. [All 30 comparisons](../benchmarks/results/phase8-final-20261013.md), [ordered JSON](../benchmarks/results/phase8-final-20261013.json), [CSV](../benchmarks/results/phase8-final-20261013.csv), and [exact final source bytes](../benchmarks/results/phase8-final-source.json) preserve provenance. [Acceptance checks and paired CPU changes](evidence/phase8-performance-acceptance.json) independently validate the completed report.

All cases (three pairs each; positive CPU reduction means less native CPU time):

| Size | Server | Mode | Median paired rate change | Median paired CPU reduction | Peak optimized MiB |
|---|---|---|---:|---:|---:|
| 100MB | unlimited | 4 | +7.1% | +0.0% | 9.20 |
| 100MB | unlimited | 8 | +20.7% | +12.1% | 9.50 |
| 100MB | unlimited | stream | +0.8% | +8.3% | 8.66 |
| 100MB | unlimited | auto | +11.7% | +23.7% | 9.20 |
| 100MB | unlimited | 1 | +1.3% | +10.8% | 9.02 |
| 100MB | unlimited | 16 | +16.4% | -3.3% | 10.08 |
| 100MB | per-connection | auto | +0.6% | -13.7% | 10.19 |
| 100MB | per-connection | 4 | -0.4% | +0.0% | 9.20 |
| 100MB | per-connection | 16 | +1.8% | +2.3% | 10.08 |
| 100MB | per-connection | stream | +0.1% | +3.6% | 8.66 |
| 100MB | per-connection | 8 | -0.5% | +8.9% | 9.50 |
| 100MB | per-connection | 1 | -0.2% | -7.7% | 8.91 |
| 100MB | shared | 8 | +1.0% | -20.3% | 9.48 |
| 100MB | shared | stream | +0.7% | -26.9% | 8.65 |
| 100MB | shared | auto | +0.4% | +17.7% | 9.55 |
| 100MB | shared | 1 | +0.4% | +4.6% | 8.89 |
| 100MB | shared | 4 | -0.2% | -53.4% | 9.22 |
| 100MB | shared | 16 | +1.4% | +19.7% | 10.02 |
| 1GB | unlimited | stream | +2.3% | -1.3% | 8.65 |
| 1GB | unlimited | 16 | +22.6% | +18.5% | 10.19 |
| 1GB | unlimited | 1 | -11.7% | +8.9% | 9.34 |
| 1GB | unlimited | 8 | +22.8% | +17.5% | 9.79 |
| 1GB | unlimited | auto | +27.1% | +20.1% | 10.25 |
| 1GB | unlimited | 4 | +19.5% | +14.6% | 9.57 |
| 5GB | unlimited | stream | +2.4% | +2.8% | 8.66 |
| 5GB | unlimited | 4 | +32.5% | +22.0% | 10.42 |
| 5GB | unlimited | 8 | +28.5% | +15.8% | 10.59 |
| 5GB | unlimited | auto | +16.8% | +13.1% | 9.68 |
| 5GB | unlimited | 1 | +0.0% | +12.2% | 10.12 |
| 5GB | unlimited | 16 | +21.0% | +16.6% | 11.02 |

These observed changes include both improvements and regressions; they do not establish a universal speed or CPU advantage. Connection-policy effects, repeated hashes/checkpoint flushes, startup, and variable host conditions contribute to the end-to-end result. Consult individual paired changes and all fixed-count cases in the full report before drawing conclusions. Larger checksum/segment buffers use additional bounded memory to reduce calls. Traffic caps constrain the achievable wall-time gain.

Automatic mode's observed median paired gains were 11.7%, 27.1%, and 16.8% for unlimited 100 MB, 1 GB, and 5 GB respectively. The 1 GB single-slot case regressed 11.7%, and capped cases did not consistently reduce CPU. Variability is substantial: the three 5 GB eight-slot paired changes were -34.5%, +33.5%, and +28.5%. Its positive paired median coexists with a lower optimized group-median rate; those different statistics must not be substituted for each other. Three pairs are too few to establish a stable causal speedup across host conditions.

Final correctness verification passed 12/12 groups in both Release and Debug plus nine actual Chrome checks in each configuration. The earlier desktop-test failure, focused rerun, and final passing suites are retained in [verification notes](evidence/phase8-debug-desktop-observation.json). Its external cause remains unestablished.

Process wall time includes native startup, transfer, hashes, flushes, and shutdown, excluding independent rereads. Native CPU/working set exclude the Python fixture and OS file cache. Cached write-call timings are not physical disk bandwidth; overlapping timings cannot be added to wall time. Payload files are sequentially removed; 5 GB cases require that much free space plus a 1 GiB allowance. Three pairs do not establish statistical significance; inspect individual changes and spread. HTTP/2 wire throughput, physical networks/storage, interrupted throughput, cold caches, other machines, and broader repetitions remain unmeasured.

## IDM availability

Installed IDM did not request the fixture or produce a file within either final documented-CLI 30-second availability probe. No cause or comparative speed is inferred. [IDM status](IDM_COMPARISON.md) contains evidence and reproduction instructions.
