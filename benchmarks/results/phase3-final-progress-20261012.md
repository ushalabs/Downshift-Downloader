# Downshift Phase 3 local benchmarks

Completed: True. SHA-256 verified samples: 18.

Sizes are decimal MB/GB; rates use MiB/s. New client process per run; OS caches stay enabled. Manual modes use fixed 4 MiB chunks; automatic uses adaptive chunks unless the run metadata says otherwise. These are controlled loopback results, not Internet or IDM comparisons.

| Size | Server | Mode | Runs | Median client wall s | Median transfer MiB/s | Median end-to-end MiB/s | Peak client working set MiB |
|---|---|---|---:|---:|---:|---:|---:|
| 100MB | unlimited | 4 | 3 | 0.451 | 453.40 | 211.68 | 7.25 |
| 100MB | unlimited | 8 | 3 | 0.414 | 424.52 | 230.16 | 7.33 |
| 100MB | unlimited | stream | 3 | 0.388 | 566.37 | 245.55 | 7.20 |
| 100MB | unlimited | auto | 3 | 0.414 | 449.95 | 230.26 | 7.24 |
| 100MB | unlimited | 1 | 3 | 0.596 | 249.38 | 160.03 | 7.23 |
| 100MB | unlimited | 16 | 3 | 0.467 | 351.00 | 204.04 | 7.38 |

Raw JSON/CSV include sampled peak rate, process CPU time/utilization, logical file write bytes/time, checksum/flush/allocation time, connection setup/first-byte time, range sizes, active-request limits, and tuner changes. Write timing measures Windows write calls into its cache, not physical disk throughput. Connection setup times are summed across requests and may overlap.

Server shaping reserves 64 KiB blocks on monotonic deadlines. Per-connection cap: 4 MiB/s with 25 ms header delay; shared cap: 16 MiB/s with 25 ms header delay. Large files use only the unlimited profile by default; shaped 1/5 GB cases can be explicitly enabled. Streaming runs do not segment, irrespective of configured connection slots.

Retry/recovery and direct IDM comparisons were not measured. HTTP/2 capability is enabled in the client, but this server speaks HTTP/1.1. A Python server on the same machine, cached writes, and loopback CPU scheduling limit generalization.
