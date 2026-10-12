"""Real adaptive-policy correctness checks, independently verified by SHA-256."""
import collections
import json
import math
import pathlib
import subprocess
import sys
import tempfile
import threading
import time

from PerformanceServer import ControlledServer, expected_sha256, file_sha256, local_environment


def main():
    cli, unit = map(pathlib.Path, sys.argv[1:3])
    server = ControlledServer()
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    count = 0
    try:
        with tempfile.TemporaryDirectory(prefix="performance-test-", dir=pathlib.Path.cwd()) as temp:
            directory = pathlib.Path(temp)
            def run(name, size, arguments=(), stream=False, observer=False, **conditions):
                nonlocal count
                server.configure(size, **conditions)
                expected = expected_sha256(size)
                output = directory / (name + ".bin")
                url = f"http://127.0.0.1:{server.server_port}/" + ("stream" if stream else "range")
                command = [str(unit), "--observer-failure", url, str(output)] if observer else [str(cli), url, str(output),
                    "--sha256", expected, "--json", "--progress-json", *arguments]
                started = time.perf_counter()
                result = subprocess.run(command, capture_output=True, text=True, timeout=40, env=local_environment())
                assert result.returncode == 0, f"{name}: {result.stderr}"
                assert output.stat().st_size == size and file_sha256(output) == expected, f"{name}: incorrect file"
                assert not output.with_name(output.name + ".part").exists()
                if observer:
                    count += 1; print(f"PASS {name}", flush=True); return None, []
                metrics = json.loads(result.stdout)
                snapshots = [json.loads(line) for line in result.stderr.splitlines()]
                assert metrics["bytes"] == size and metrics["network_bytes"] == size and metrics["write_bytes"] == size
                assert metrics["average_bps"] > 0 and metrics["peak_bps"] > 0
                assert 0 < metrics["transfer_seconds"] <= metrics["total_seconds"] <= time.perf_counter() - started
                assert metrics["resource_metrics_available"] and metrics["peak_working_set_bytes"] > 0
                assert snapshots[0]["stage"] == 0 and snapshots[-1]["stage"] == 3
                assert snapshots[-1]["downloaded_bytes"] == size and snapshots[-1]["verified_bytes"] == size
                assert snapshots[-1]["eta_seconds"] == 0
                times = [p["elapsed_seconds"] for p in snapshots]
                assert times == sorted(times)
                bytes_ = [p["downloaded_bytes"] for p in snapshots]
                assert bytes_ == sorted(bytes_) and all(b <= size for b in bytes_)
                for p in snapshots:
                    for key in ("instant_bps", "smoothed_bps", "average_bps"):
                        assert math.isfinite(p[key]) and p[key] >= 0
                if metrics["segmented"]:
                    ranges = sorted((r["first"], r["last"]) for r in server.records if not r["probe"])
                    assert ranges[0][0] == 0 and ranges[-1][1] == size - 1
                    assert all(ranges[i][1] + 1 == ranges[i + 1][0] for i in range(len(ranges) - 1)), "Gap or overlap"
                    assert all(r["if_match"] == f'"pattern-{size}"' for r in server.records if not r["probe"])
                count += 1; print(f"PASS {name}: {metrics['average_bps'] / 1048576:.2f} MiB/s", flush=True)
                return metrics, snapshots

            for connections in (1, 4, 8, 16):
                metrics, snapshots = run(f"manual-{connections}", 8 * 1024 * 1024,
                    ("--connections", str(connections), "--chunk-kib", "256"), header_delay=0.02)
                assert metrics["tuning_changes"] == 0 and metrics["final_connections"] == connections
                assert server.peak <= connections and metrics["peak_active_ranges"] <= connections
                assert metrics["ranges"] == 32
            metrics, _ = run("automatic-small", 256 * 1024)
            assert metrics["initial_connections"] == 1 and metrics["peak_active_ranges"] == 1
            metrics, snapshots = run("automatic-growth", 64 * 1024 * 1024, per_connection_bps=2 * 1024 * 1024)
            assert metrics["tuning_changes"] > 0 and max(p["target_connections"] for p in snapshots) > metrics["initial_connections"]
            assert metrics["largest_range_bytes"] != metrics["smallest_range_bytes"]
            assert metrics["peak_active_ranges"] <= 16 and server.peak <= 16
            metrics, snapshots = run("automatic-plateau", 64 * 1024 * 1024, total_bps=8 * 1024 * 1024)
            targets = [p["target_connections"] for p in snapshots if p["stage"] == 1]
            assert max(targets) > metrics["initial_connections"] and metrics["final_connections"] < max(targets), "Plateau trial was not rolled back"
            metrics, _ = run("heterogeneous-workers", 8 * 1024 * 1024,
                ("--connections", "4", "--chunk-kib", "256"), per_connection_bps=4 * 1024 * 1024, heterogeneous=True)
            counts = collections.Counter(r["connection"] for r in server.records if not r["probe"])
            assert len(counts) == 4 and max(counts.values()) > 2 * min(counts.values()), "Fast workers did not receive more ranges"
            metrics, snapshots = run("automatic-streaming", 8 * 1024 * 1024, stream=True, total_bps=16 * 1024 * 1024)
            assert not metrics["segmented"] and metrics["final_connections"] == 1
            metrics, snapshots = run("weak-validator-delay", 4 * 1024 * 1024, header_delay=0.6, weak_etag=True)
            pending = [p for p in snapshots if p["stage"] == 1 and p["downloaded_bytes"] == 0]
            # Full headers can arrive before the first body bytes. Either unknown
            # or the actual full size is valid here; the one-byte probe is not.
            assert not metrics["segmented"] and any(p["total_bytes"] is None for p in pending)
            assert all(p["total_bytes"] in (None, metrics["bytes"]) and p["eta_seconds"] is None for p in pending), "Probe length leaked into streaming progress"
            run("observer-exception", 4 * 1024 * 1024, observer=True)
            for flag, value in (("--connections", "0"), ("--connections", "17"), ("--connections", "bad"), ("--chunk-kib", "0")):
                result = subprocess.run([str(cli), "http://127.0.0.1:1/range", str(directory / "invalid.bin"), flag, value], capture_output=True, env=local_environment())
                assert result.returncode != 0 and not (directory / "invalid.bin.part").exists()
            print("PASS invalid-policy-options", flush=True); count += 1
    finally:
        server.shutdown(); server.server_close(); thread.join(2)
    print(f"{count} adaptive scenarios passed.", flush=True)


if __name__ == "__main__": main()
