# Downshift local benchmarks

Completed: True. SHA-256 verified samples: 36.

Sizes are decimal MB/GB; rates use MiB/s. New client process per run; OS caches stay enabled. Manual modes use fixed 4 MiB chunks; automatic uses adaptive chunks unless the run metadata says otherwise. These are controlled loopback results, not Internet or IDM comparisons.

| Size | Server | Mode | Runs | Median client wall s | Median transfer MiB/s | Median end-to-end MiB/s | Peak client working set MiB |
|---|---|---|---:|---:|---:|---:|---:|
| 100MB | unlimited | 4 | 3 | 0.444 | 267.61 | 214.65 | 7.78 |
| 100MB | unlimited | 8 | 3 | 0.445 | 268.37 | 214.15 | 7.83 |
| 100MB | unlimited | stream | 3 | 0.220 | 1059.73 | 433.56 | 7.73 |
| 100MB | unlimited | auto | 3 | 0.427 | 279.37 | 223.37 | 7.78 |
| 100MB | unlimited | 1 | 3 | 0.696 | 156.24 | 137.10 | 7.76 |
| 100MB | unlimited | 16 | 3 | 0.422 | 290.23 | 226.12 | 7.93 |
| 1GB | unlimited | 16 | 3 | 4.215 | 276.65 | 226.24 | 8.18 |
| 1GB | unlimited | stream | 3 | 1.936 | 1112.09 | 492.57 | 7.71 |
| 1GB | unlimited | 4 | 3 | 4.342 | 266.93 | 219.66 | 8.07 |
| 1GB | unlimited | 8 | 3 | 4.243 | 272.06 | 224.76 | 8.20 |
| 1GB | unlimited | auto | 3 | 4.198 | 276.53 | 227.19 | 7.92 |
| 1GB | unlimited | 1 | 3 | 6.059 | 182.65 | 157.40 | 8.03 |

Raw JSON/CSV include sampled peak rate, process CPU time/utilization, logical file write bytes/time, checksum/flush/allocation time, connection setup/first-byte time, range sizes, active-request limits, and tuner changes. Write timing measures Windows write calls into its cache, not physical disk throughput. Connection setup times are summed across requests and may overlap.

Server shaping reserves 64 KiB blocks on monotonic deadlines. Per-connection cap: 4 MiB/s with 25 ms header delay; shared cap: 16 MiB/s with 25 ms header delay. Large files use only the unlimited profile by default; shaped 1/5 GB cases can be explicitly enabled. Streaming runs do not segment, irrespective of configured connection slots.

Interrupted/retried throughput and direct IDM comparisons were not measured. HTTP/2 capability is enabled in the client, but this server speaks HTTP/1.1. A Python server on the same machine, cached writes, and loopback CPU scheduling limit generalization.
