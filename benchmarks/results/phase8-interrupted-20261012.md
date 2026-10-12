# Phase 8 paired local benchmarks

Completed: False. Independently verified samples: 137.

Release builds alternate in a deterministic shuffled order on the same server. Three repetitions by default. Decimal MB/GB; MiB/s rates. OS caches stay enabled. Both clients perform the same full SHA-256 verification. This is controlled HTTP/1.1 loopback, not Internet, physical-disk, or IDM performance.

| Size | Server | Mode | Pairs | Baseline MiB/s | Optimized MiB/s | Median paired rate change | Baseline CPU s | Optimized CPU s | Optimized peak MiB |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| 100MB | unlimited | 4 | 3 | 67.26 | 79.44 | +17.1% | 1.203 | 1.000 | 9.20 |
| 100MB | unlimited | 8 | 3 | 66.83 | 76.45 | +15.8% | 1.219 | 1.141 | 9.48 |
| 100MB | unlimited | stream | 3 | 101.53 | 104.39 | +4.2% | 0.750 | 0.609 | 8.96 |
| 100MB | unlimited | auto | 3 | 61.01 | 74.45 | +22.0% | 1.328 | 1.078 | 9.20 |
| 100MB | unlimited | 1 | 3 | 55.37 | 52.90 | +3.0% | 1.156 | 1.094 | 9.00 |
| 100MB | unlimited | 16 | 3 | 62.65 | 74.62 | +24.6% | 1.297 | 1.031 | 10.08 |
| 100MB | per-connection | auto | 3 | 22.07 | 22.17 | +0.8% | 1.719 | 1.578 | 10.23 |
| 100MB | per-connection | 4 | 3 | 13.17 | 13.11 | +0.1% | 1.375 | 1.312 | 9.22 |
| 100MB | per-connection | 16 | 3 | 31.82 | 31.69 | -0.3% | 1.219 | 1.156 | 10.09 |
| 100MB | per-connection | stream | 3 | 3.69 | 3.69 | +0.1% | 1.062 | 0.906 | 8.84 |
| 100MB | per-connection | 8 | 3 | 23.46 | 23.31 | -0.5% | 1.141 | 1.266 | 9.52 |
| 100MB | per-connection | 1 | 3 | 3.51 | 3.51 | +0.1% | 1.422 | 1.406 | 8.89 |
| 100MB | shared | 8 | 3 | 14.42 | 14.32 | -0.9% | 1.125 | 0.984 | 9.50 |
| 100MB | shared | stream | 3 | 12.35 | 12.33 | -0.2% | 1.188 | 0.859 | 8.85 |
| 100MB | shared | auto | 3 | 14.52 | 14.66 | +0.8% | 1.703 | 1.219 | 9.54 |
| 100MB | shared | 1 | 3 | 10.55 | 10.55 | -0.0% | 1.297 | 1.203 | 8.89 |
| 100MB | shared | 4 | 3 | 14.59 | 14.59 | +0.2% | 1.047 | 1.297 | 9.22 |
| 100MB | shared | 16 | 3 | 14.59 | 14.55 | -0.3% | 1.922 | 1.891 | 10.02 |
| 1GB | unlimited | stream | 3 | 124.36 | 145.40 | +18.0% | 7.188 | 5.953 | 8.96 |
| 1GB | unlimited | 16 | 2 | 67.75 | 82.88 | +22.3% | 11.961 | 10.094 | 10.30 |
| 1GB | unlimited | 1 | 3 | 58.70 | 55.33 | -9.5% | 10.984 | 10.797 | 9.35 |
| 1GB | unlimited | 8 | 2 | 68.20 | 82.90 | +21.6% | 12.117 | 10.086 | 9.76 |
| 1GB | unlimited | auto | 2 | 69.00 | 85.32 | +23.7% | 11.859 | 9.781 | 10.15 |
| 1GB | unlimited | 4 | 2 | 65.48 | 81.42 | +24.3% | 12.406 | 10.211 | 9.50 |

All files are reread for size/SHA-256 after client completion. Exact contiguous nonoverlapping range coverage, byte totals, and connection limits must pass before acceptance. Process wall time includes native startup/checksum/flush/shutdown and excludes the independent reread. CPU/memory are native process measurements; write calls are cached Windows I/O.

Per-connection cap: 4 MiB/s; shared cap: 16 MiB/s; shaped profiles have 25 ms header delay. Large files use unlimited traffic unless explicitly requested. Fixed modes use 4 MiB chunks; auto uses adaptive chunks in both builds. Partial reports retain complete pairs only for comparisons. Three repetitions do not establish statistical significance; inspect individual paired changes and spread.
