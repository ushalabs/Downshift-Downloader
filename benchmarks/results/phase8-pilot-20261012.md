# Phase 8 paired local benchmarks

Completed: True. Independently verified samples: 36.

Release builds alternate in a deterministic shuffled order on the same server. Three repetitions by default. Decimal MB/GB; MiB/s rates. OS caches stay enabled. Both clients perform the same full SHA-256 verification. This is controlled HTTP/1.1 loopback, not Internet, physical-disk, or IDM performance.

| Size | Server | Mode | Pairs | Baseline MiB/s | Optimized MiB/s | Median paired rate change | Baseline CPU s | Optimized CPU s | Optimized peak MiB |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| 100MB | unlimited | stream | 3 | 130.74 | 197.48 | +53.0% | 0.375 | 0.281 | 8.71 |
| 100MB | unlimited | 4 | 3 | 147.56 | 133.17 | +2.1% | 0.578 | 0.516 | 8.99 |
| 100MB | unlimited | auto | 3 | 118.14 | 116.76 | -0.1% | 0.578 | 0.641 | 8.97 |
| 1GB | unlimited | stream | 3 | 281.30 | 214.47 | -23.6% | 2.844 | 2.766 | 8.73 |
| 1GB | unlimited | 4 | 3 | 132.02 | 148.62 | +12.6% | 5.844 | 5.156 | 9.29 |
| 1GB | unlimited | auto | 3 | 150.58 | 166.89 | +10.8% | 5.094 | 4.625 | 10.04 |

All files are reread for size/SHA-256 after client completion. Exact contiguous nonoverlapping range coverage, byte totals, and connection limits must pass before acceptance. Process wall time includes native startup/checksum/flush/shutdown and excludes the independent reread. CPU/memory are native process measurements; write calls are cached Windows I/O.

Per-connection cap: 4 MiB/s; shared cap: 16 MiB/s; shaped profiles have 25 ms header delay. Large files use unlimited traffic unless explicitly requested. Fixed modes use 4 MiB chunks; auto uses adaptive chunks in both builds. Partial reports retain complete pairs only for comparisons. Three repetitions do not establish statistical significance; inspect individual paired changes and spread.
