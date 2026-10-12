# Downshift Phase 3 local benchmarks

Completed: True. SHA-256 verified samples: 90.

Sizes are decimal MB/GB; rates use MiB/s. New client process per run; OS caches stay enabled. Manual modes use fixed 4 MiB chunks; automatic uses adaptive chunks unless the run metadata says otherwise. These are controlled loopback results, not Internet or IDM comparisons.

| Size | Server | Mode | Runs | Median client wall s | Median transfer MiB/s | Median end-to-end MiB/s | Peak client working set MiB |
|---|---|---|---:|---:|---:|---:|---:|
| 100MB | unlimited | 4 | 3 | 1.028 | 177.91 | 92.77 | 7.23 |
| 100MB | unlimited | 8 | 3 | 0.971 | 188.12 | 98.26 | 7.34 |
| 100MB | unlimited | stream | 3 | 0.976 | 200.06 | 97.67 | 7.20 |
| 100MB | unlimited | auto | 3 | 0.969 | 191.30 | 98.38 | 7.24 |
| 100MB | unlimited | 1 | 3 | 1.146 | 136.34 | 83.18 | 7.23 |
| 100MB | unlimited | 16 | 3 | 1.029 | 163.91 | 92.71 | 7.37 |
| 100MB | per-connection | 16 | 3 | 2.649 | 42.86 | 36.01 | 7.33 |
| 100MB | per-connection | stream | 3 | 25.783 | 3.75 | 3.70 | 7.19 |
| 100MB | per-connection | 4 | 3 | 7.024 | 14.46 | 13.58 | 7.23 |
| 100MB | per-connection | 8 | 3 | 3.729 | 28.83 | 25.58 | 7.28 |
| 100MB | per-connection | auto | 3 | 4.069 | 26.12 | 23.44 | 7.44 |
| 100MB | per-connection | 1 | 3 | 26.339 | 3.67 | 3.62 | 7.23 |
| 100MB | shared | 16 | 3 | 6.453 | 15.81 | 14.78 | 7.34 |
| 100MB | shared | stream | 3 | 7.745 | 12.93 | 12.31 | 7.22 |
| 100MB | shared | 4 | 3 | 6.463 | 15.77 | 14.76 | 7.24 |
| 100MB | shared | auto | 3 | 6.458 | 15.78 | 14.77 | 7.28 |
| 100MB | shared | 1 | 3 | 8.364 | 11.93 | 11.40 | 7.22 |
| 100MB | shared | 8 | 3 | 6.431 | 15.83 | 14.83 | 7.28 |
| 1GB | unlimited | 1 | 3 | 9.200 | 165.07 | 103.66 | 7.21 |
| 1GB | unlimited | 8 | 3 | 4.735 | 433.56 | 201.42 | 7.38 |
| 1GB | unlimited | 16 | 3 | 3.746 | 459.92 | 254.58 | 7.50 |
| 1GB | unlimited | 4 | 3 | 3.296 | 582.20 | 289.35 | 7.26 |
| 1GB | unlimited | stream | 3 | 7.604 | 247.49 | 125.41 | 7.20 |
| 1GB | unlimited | auto | 3 | 4.459 | 414.64 | 213.87 | 7.29 |
| 5GB | unlimited | 16 | 3 | 36.302 | 205.38 | 131.35 | 7.51 |
| 5GB | unlimited | auto | 3 | 17.066 | 415.09 | 279.41 | 7.24 |
| 5GB | unlimited | 1 | 3 | 39.379 | 204.33 | 121.09 | 7.22 |
| 5GB | unlimited | stream | 3 | 16.078 | 505.24 | 296.58 | 7.19 |
| 5GB | unlimited | 8 | 3 | 18.918 | 362.18 | 252.06 | 7.39 |
| 5GB | unlimited | 4 | 3 | 30.010 | 329.07 | 158.89 | 7.26 |

Raw JSON/CSV include sampled peak rate, process CPU time/utilization, logical file write bytes/time, checksum/flush/allocation time, connection setup/first-byte time, range sizes, active-request limits, and tuner changes. Write timing measures Windows write calls into its cache, not physical disk throughput. Connection setup times are summed across requests and may overlap.

Server shaping reserves 64 KiB blocks on monotonic deadlines. Per-connection cap: 4 MiB/s with 25 ms header delay; shared cap: 16 MiB/s with 25 ms header delay. Large files use only the unlimited profile by default; shaped 1/5 GB cases can be explicitly enabled. Streaming runs do not segment, irrespective of configured connection slots.

Retry/recovery and direct IDM comparisons were not measured. HTTP/2 capability is enabled in the client, but this server speaks HTTP/1.1. A Python server on the same machine, cached writes, and loopback CPU scheduling limit generalization.
