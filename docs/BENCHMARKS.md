# Downshift benchmarks

Phase 8 adds a paired comparison against the preserved Phase 7 Release executable. Its measured bottlenecks, bounded buffering changes, reproduction steps, and current evidence are in [Phase 8 performance](PERFORMANCE.md). [IDM comparison status](IDM_COMPARISON.md) records the installed comparator's unsuccessful availability probes; no IDM throughput is claimed.

The historical Phase 3/4 evidence below describes earlier engines. All these results describe controlled HTTP/1.1 loopback traffic on the development machine; they do not establish Internet throughput or universal superiority over another application.

## Reproduce

Build the Release client and run the correctness tests first. From the project folder with Python 3.10+:

```powershell
python scripts/Benchmark.py
```

The default matrix uses decimal 100 MB, 1 GB, and 5 GB files with fixed 1/4/8/16 slots, automatic mode, and a streaming case. Each case has three repetitions with a deterministic shuffled order. Manual cases use fixed 4 MiB chunks; automatic mode uses adaptive chunks. This compares complete policies, not the isolated causal effect of connection count. Use `--fixed-auto-chunks` for an otherwise equivalent comparison with fixed chunks in automatic mode.

Server profiles: unlimited loopback; 4 MiB/s per connection with 25 ms header delay; and a shared 16 MiB/s cap with 25 ms header delay. Large files use only unlimited traffic by default, avoiding very long capped runs. Use `--shape-large-files` to run the capped 1/5 GB cases. All profiles are also exercised without segmentation on the 100 MB file. Streaming 1/5 GB cases run with the unlimited profile.

For a smaller matrix:

```powershell
python scripts/Benchmark.py --sizes 100MB --profiles unlimited --repetitions 3
```

`--output benchmarks/results/your-fresh-name` selects a new report name. Existing result JSON files are never overwritten. Interrupted runs retain accepted samples with `complete: false`; do not treat them as completed matrices. Generated payloads live in a unique directory under `build/benchmark-files` and are removed after verification. The largest default file needs about 5 GB of available disk space plus a 1 GiB safety allowance. Results contain only measurements, not the payloads.

## Measurements and integrity

The server generates a repeatable byte pattern in bounded memory, with monotonic 64 KiB pacing deadlines. Every client run receives a known SHA-256. The runner independently rereads the completed file, verifies its full SHA-256 and size, and checks contiguous nonoverlapping server request records and hard concurrency limits before accepting a sample.

Raw JSON/CSV record client process wall time, engine transfer/finalization time, average/peak accepted-payload rate, process CPU time/utilization, peak working set, logical file-write bytes/time, allocation/checksum/flush time, new connections, setup and first-byte timings, assigned ranges, and tuning decisions. Markdown reports show medians and maximum measured client working set per group. Sizes use decimal MB/GB; throughput uses MiB/s (1048576 bytes).

Transfer time includes probing/allocation and ends before checksum/flush. End-to-end rate includes native process startup, hash verification, flush, and shutdown, but excludes the runner's independent reread. CPU is the native process's sampled CPU time, not the Python server's. Resource availability is explicit. Peak rate uses positive windows of at least 250 ms, falling back to a shorter stage sample for small/fast files. Header/probe/TLS/TCP bytes are excluded; fallback payload writes would be counted separately from final goodput.

Write-call timing is cached Windows I/O, not physical disk bandwidth. Connection setup timings are summed across requests, include lookup/TCP/TLS where applicable, and can overlap; they are not an additive wall-time penalty. Process memory is distinct from the OS file cache. Source fingerprints, executable SHA-256, compiler/SDK/dependency versions, hardware, shaping settings, and run order are retained.

## Local evidence

The completed main matrix is in [phase3-local-20261012.md](../benchmarks/results/phase3-local-20261012.md), with matching JSON/CSV: 90 accepted samples, three repetitions per group, covering decimal 100 MB, 1 GB, and 5 GB. Every sample passed native and independent SHA-256/size verification and request-coverage checks. Measured source bytes are archived in `benchmarks/results/phase3-local-20261012-source.json` for this working-tree build.

Final review found that a range probe's one-byte Content-Length could briefly appear as the streaming total while waiting for full headers. The final code now leaves total/ETA unknown until the streaming headers arrive, with a slow-header/weak-validator regression check. This changes pending-header progress only; the transfer, tuning, checksum, and measurement logic used by the main matrix is unchanged. The source archive preserves the exact earlier measured build. The completed [final executable supplement](../benchmarks/results/phase3-final-progress-20261012.md) records 18 additional verified 100 MB unlimited samples after both corrected Debug/Release suites passed. Its metadata separately fingerprints the final executable and source. The reports contain 108 accepted samples in total.

Main-matrix median end-to-end rates show the policy's tradeoffs:

| Case | Fixed 4 slots | Automatic | Observation |
|---|---:|---:|---|
| 100 MB, 4 MiB/s per connection | 13.58 MiB/s | 23.44 MiB/s | Automatic improved by about 73%; fixed 16 was faster at 36.01 MiB/s. |
| 100 MB, shared 16 MiB/s cap | 14.76 MiB/s | 14.77 MiB/s | Additional connections offer little benefit after saturation. |
| 1 GB, unlimited loopback | 289.35 MiB/s | 213.87 MiB/s | Automatic was slower than the fixed baseline. |
| 5 GB, unlimited loopback | 158.89 MiB/s | 279.41 MiB/s | Large variation between repetitions; streaming reached 296.58 MiB/s. |

Maximum sampled native-client peak working set was 7.51 MiB in the main matrix and 7.38 MiB in the supplement, excluding server memory and OS cache. The supplement's unlimited 100 MB medians were 211.68 MiB/s for fixed 4 and 230.26 MiB/s for automatic. Their difference from the earlier loopback runs illustrates sensitivity to machine/cache conditions; do not combine repetitions across the two builds or infer a speed improvement from the progress correction.

## Interpretation and later work

Phase 3's final sources are in the user's `d7d9a1e55320bf4ddb304e58ae6b807775d1a823` commit; those reports describe the earlier engine. Current Phase 4 sources and executable have separate fingerprints in [phase4-local-20261012.md](../benchmarks/results/phase4-local-20261012.md) and its matching JSON/CSV. All 18 Phase 4 samples passed native/independent SHA-256 and coverage/limit checks. This is a 100 MB unlimited matrix with three repetitions of all six modes. Median end-to-end rates were 232.80 MiB/s for fixed 4, 221.88 MiB/s for automatic, and 396.98 MiB/s for streaming. The run measures fresh downloads with recovery/checkpoint overhead; it does not measure interrupted/retried throughput. OS/cache variation prevents treating separate phase runs as an isolated causal comparison.

A conservative automatic policy can improve throughput when each connection is capped, but fixed high concurrency can still finish sooner. When total server bandwidth is shared, extra connections cannot create more capacity. Uncapped loopback results depend on Python scheduling, CPU frequency, OS caching, and storage; three repetitions can still have substantial spread. Inspect individual samples before interpreting medians.

The benchmark server shares the machine with the client; these are warm/cache-enabled runs, not a dedicated physical-network lab. HTTP/2 is enabled in the client but not exercised by this server. Phase 4 verifies retry/recovery correctness separately in the recovery suite. Interrupted/retried throughput, HTTP/2 wire benchmarks, physical-disk characterization, wider repetitions/network conditions, and a fair direct IDM comparison remain later performance work. No IDM numbers or unexecuted results are fabricated.
