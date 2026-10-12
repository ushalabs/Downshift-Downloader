# Phase 8 paired local benchmarks

Completed: True. Independently verified samples: 180.

Release builds alternate in a deterministic shuffled order on the same server. Three repetitions by default. Decimal MB/GB; MiB/s rates. OS caches stay enabled. Both clients perform the same full SHA-256 verification. This is controlled HTTP/1.1 loopback, not Internet, physical-disk, or IDM performance.

| Size | Server | Mode | Pairs | Baseline MiB/s | Optimized MiB/s | Median paired rate change | Baseline CPU s | Optimized CPU s | Optimized peak MiB |
|---|---|---|---:|---:|---:|---:|---:|---:|---:|
| 100MB | unlimited | 4 | 3 | 136.83 | 153.71 | +7.1% | 0.516 | 0.453 | 9.20 |
| 100MB | unlimited | 8 | 3 | 125.61 | 151.66 | +20.7% | 0.547 | 0.516 | 9.50 |
| 100MB | unlimited | stream | 3 | 218.96 | 210.56 | +0.8% | 0.344 | 0.328 | 8.66 |
| 100MB | unlimited | auto | 3 | 124.41 | 140.54 | +11.7% | 0.562 | 0.453 | 9.20 |
| 100MB | unlimited | 1 | 3 | 81.11 | 82.16 | +1.3% | 0.562 | 0.516 | 9.02 |
| 100MB | unlimited | 16 | 3 | 143.88 | 161.30 | +16.4% | 0.469 | 0.484 | 10.08 |
| 100MB | per-connection | auto | 3 | 22.97 | 23.10 | +0.6% | 1.062 | 1.078 | 10.19 |
| 100MB | per-connection | 4 | 3 | 13.83 | 13.69 | -0.4% | 0.781 | 0.859 | 9.20 |
| 100MB | per-connection | 16 | 3 | 35.94 | 37.48 | +1.8% | 0.703 | 0.672 | 10.08 |
| 100MB | per-connection | stream | 3 | 3.69 | 3.69 | +0.1% | 0.922 | 1.016 | 8.66 |
| 100MB | per-connection | 8 | 3 | 25.64 | 25.80 | -0.5% | 0.734 | 0.734 | 9.50 |
| 100MB | per-connection | 1 | 3 | 3.55 | 3.51 | -0.2% | 1.219 | 1.312 | 8.91 |
| 100MB | shared | 8 | 3 | 14.45 | 14.59 | +1.0% | 1.234 | 1.484 | 9.48 |
| 100MB | shared | stream | 3 | 12.27 | 12.40 | +0.7% | 0.828 | 1.078 | 8.65 |
| 100MB | shared | auto | 3 | 14.57 | 14.66 | +0.4% | 1.500 | 1.328 | 9.55 |
| 100MB | shared | 1 | 3 | 10.47 | 10.51 | +0.4% | 1.359 | 1.297 | 8.89 |
| 100MB | shared | 4 | 3 | 14.47 | 14.45 | -0.2% | 0.984 | 1.391 | 9.22 |
| 100MB | shared | 16 | 3 | 14.41 | 14.54 | +1.4% | 1.031 | 0.859 | 10.02 |
| 1GB | unlimited | stream | 3 | 121.73 | 123.47 | +2.3% | 7.172 | 7.219 | 8.65 |
| 1GB | unlimited | 16 | 3 | 68.08 | 83.24 | +22.6% | 11.922 | 9.594 | 10.19 |
| 1GB | unlimited | 1 | 3 | 59.55 | 52.58 | -11.7% | 11.594 | 10.641 | 9.34 |
| 1GB | unlimited | 8 | 3 | 67.46 | 83.51 | +22.8% | 12.188 | 10.062 | 9.79 |
| 1GB | unlimited | auto | 3 | 66.57 | 84.09 | +27.1% | 12.312 | 9.844 | 10.25 |
| 1GB | unlimited | 4 | 3 | 65.89 | 78.75 | +19.5% | 12.453 | 10.641 | 9.57 |
| 5GB | unlimited | stream | 3 | 131.32 | 134.48 | +2.4% | 33.875 | 33.922 | 8.66 |
| 5GB | unlimited | 4 | 3 | 62.31 | 87.48 | +32.5% | 61.422 | 46.844 | 10.42 |
| 5GB | unlimited | 8 | 3 | 138.30 | 92.96 | +28.5% | 26.531 | 45.406 | 10.59 |
| 5GB | unlimited | auto | 3 | 80.69 | 93.56 | +16.8% | 51.266 | 44.562 | 9.68 |
| 5GB | unlimited | 1 | 3 | 53.61 | 78.92 | +0.0% | 60.359 | 27.844 | 10.12 |
| 5GB | unlimited | 16 | 3 | 66.44 | 80.29 | +21.0% | 59.547 | 50.109 | 11.02 |

All files are reread for size/SHA-256 after client completion. Exact contiguous nonoverlapping range coverage, byte totals, and connection limits must pass before acceptance. Process wall time includes native startup/checksum/flush/shutdown and excludes the independent reread. CPU/memory are native process measurements; write calls are cached Windows I/O.

Per-connection cap: 4 MiB/s; shared cap: 16 MiB/s; shaped profiles have 25 ms header delay. Large files use unlimited traffic unless explicitly requested. Fixed modes use 4 MiB chunks; auto uses adaptive chunks in both builds. Partial reports retain complete pairs only for comparisons. Three repetitions do not establish statistical significance; inspect individual paired changes and spread.
