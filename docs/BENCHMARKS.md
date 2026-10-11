# Downshift benchmarks

No download engine or benchmark results exist in Phase 1. No direct IDM comparison has been performed, and no performance claim is made.

## Planned methodology

Use a controlled HTTP server supporting configurable byte ranges and network limits. Measure 100 MB, 1 GB, and 5 GB files with 1, 4, 8, 16, and automatic connections. Exercise both Range-enabled and streaming fallback modes.

Verify the downloaded file size and checksum before accepting any performance sample. Record hardware, Windows/compiler/dependency versions, network conditions, server behavior, repetitions, total time, median/average/peak throughput, CPU, memory, disk writes, connection setup, retries, and recovery.

Compare IDM only if available using identical hardware, files, URLs, and comparable network conditions. Publish repeated measured results and their limitations. Optimize only measured bottlenecks, with correctness regression checks.

The reproducible suite and execution belong to Phases 3 and 8 as specified in the roadmap.
