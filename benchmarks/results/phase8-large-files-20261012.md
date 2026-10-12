# Phase 8 paired local benchmarks

Completed: False. Independently verified samples: 39.

Release builds alternate in a deterministic shuffled order on the same server. Three repetitions by default. Decimal MB/GB; MiB/s rates. OS caches stay enabled. Both clients perform the same full SHA-256 verification. This is controlled HTTP/1.1 loopback, not Internet, physical-disk, or IDM performance.

| Size | Server | Mode | Pairs | Baseline MiB/s | Optimized MiB/s | Median paired rate change | Baseline CPU s | Optimized CPU s | Optimized peak MiB |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| 1GB | unlimited | 4 | 3 | 220.20 | 285.25 | +26.8% | 3.109 | 2.547 | 9.45 |
| 1GB | unlimited | 8 | 3 | 232.38 | 271.08 | +27.2% | 2.953 | 2.797 | 9.75 |
| 1GB | unlimited | stream | 3 | 482.44 | 278.35 | -43.9% | 1.562 | 1.406 | 8.97 |
| 1GB | unlimited | auto | 3 | 228.08 | 319.01 | +39.0% | 2.641 | 2.297 | 9.56 |
| 1GB | unlimited | 1 | 3 | 156.03 | 118.43 | -24.1% | 2.953 | 2.875 | 9.32 |
| 1GB | unlimited | 16 | 3 | 231.66 | 312.93 | +44.8% | 2.984 | 2.375 | 10.29 |
| 5GB | unlimited | auto | 1 | 237.24 | 320.10 | +34.9% | 13.469 | 11.359 | 9.61 |

All files are reread for size/SHA-256 after client completion. Exact contiguous nonoverlapping range coverage, byte totals, and connection limits must pass before acceptance. Process wall time includes native startup/checksum/flush/shutdown and excludes the independent reread. CPU/memory are native process measurements; write calls are cached Windows I/O.

Per-connection cap: 4 MiB/s; shared cap: 16 MiB/s; shaped profiles have 25 ms header delay. Large files use unlimited traffic unless explicitly requested. Fixed modes use 4 MiB chunks; auto uses adaptive chunks in both builds. Partial reports retain complete pairs only for comparisons. Three repetitions do not establish statistical significance; inspect individual paired changes and spread.
