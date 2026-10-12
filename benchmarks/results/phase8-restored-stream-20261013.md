# Phase 8 paired local benchmarks

Completed: True. Independently verified samples: 6.

Release builds alternate in a deterministic shuffled order on the same server. Three repetitions by default. Decimal MB/GB; MiB/s rates. OS caches stay enabled. Both clients perform the same full SHA-256 verification. This is controlled HTTP/1.1 loopback, not Internet, physical-disk, or IDM performance.

| Size | Server | Mode | Pairs | Baseline MiB/s | Optimized MiB/s | Median paired rate change | Baseline CPU s | Optimized CPU s | Optimized peak MiB |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1GB | unlimited | stream | 3 | 284.64 | 249.58 | -12.3% | 2.766 | 3.094 | 8.65 |

All files are reread for size/SHA-256 after client completion. Exact contiguous nonoverlapping range coverage, byte totals, and connection limits must pass before acceptance. Process wall time includes native startup/checksum/flush/shutdown and excludes the independent reread. CPU/memory are native process measurements; write calls are cached Windows I/O.

Per-connection cap: 4 MiB/s; shared cap: 16 MiB/s; shaped profiles have 25 ms header delay. Large files use unlimited traffic unless explicitly requested. Fixed modes use 4 MiB chunks; auto uses adaptive chunks in both builds. Partial reports retain complete pairs only for comparisons. Three repetitions do not establish statistical significance; inspect individual paired changes and spread.
