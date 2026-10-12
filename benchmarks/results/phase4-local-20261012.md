# Downshift local benchmarks

Completed: True. SHA-256 verified samples: 18.

Sizes are decimal MB/GB; rates use MiB/s. New client process per run; OS caches stay enabled. Manual modes use fixed 4 MiB chunks; automatic uses adaptive chunks unless the run metadata says otherwise. These are controlled loopback results, not Internet or IDM comparisons.

| Size | Server | Mode | Runs | Median client wall s | Median transfer MiB/s | Median end-to-end MiB/s | Peak client working set MiB |
|---|---|---|---:|---:|---:|---:|---:|
| 100MB | unlimited | 4 | 3 | 0.410 | 297.24 | 232.80 | 7.78 |
| 100MB | unlimited | 8 | 3 | 0.435 | 275.80 | 219.01 | 7.83 |
| 100MB | unlimited | stream | 3 | 0.240 | 845.41 | 396.98 | 7.70 |
| 100MB | unlimited | auto | 3 | 0.430 | 277.94 | 221.88 | 7.79 |
| 100MB | unlimited | 1 | 3 | 0.553 | 210.53 | 172.55 | 7.75 |
| 100MB | unlimited | 16 | 3 | 0.420 | 286.30 | 227.26 | 7.89 |

Raw JSON/CSV include sampled peak rate, process CPU time/utilization, logical file write bytes/time, checksum/flush/allocation time, connection setup/first-byte time, range sizes, active-request limits, and tuner changes. Write timing measures Windows write calls into its cache, not physical disk throughput. Connection setup times are summed across requests and may overlap.

Server shaping reserves 64 KiB blocks on monotonic deadlines. Per-connection cap: 4 MiB/s with 25 ms header delay; shared cap: 16 MiB/s with 25 ms header delay. Large files use only the unlimited profile by default; shaped 1/5 GB cases can be explicitly enabled. Streaming runs do not segment, irrespective of configured connection slots.

Interrupted/retried throughput and direct IDM comparisons were not measured. HTTP/2 capability is enabled in the client, but this server speaks HTTP/1.1. A Python server on the same machine, cached writes, and loopback CPU scheduling limit generalization.
