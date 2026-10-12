"""Reproducible Release benchmarks, with verified files and bounded server memory."""
from __future__ import annotations
import argparse
import csv
import datetime
import hashlib
import json
import os
import pathlib
import platform
import random
import re
import shutil
import statistics
import subprocess
import sys
import tempfile
import threading
import time

ROOT = pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tests"))
from PerformanceServer import ControlledServer, expected_sha256, file_sha256, local_environment

SIZES = {"100MB": 100_000_000, "1GB": 1_000_000_000, "5GB": 5_000_000_000}
PROFILES = {
    "unlimited": {},
    "per-connection": {"per_connection_bps": 4 * 1024 * 1024, "header_delay": 0.025},
    "shared": {"total_bps": 16 * 1024 * 1024, "header_delay": 0.025},
}


def metadata(cli):
    processor = platform.processor()
    if os.name == "nt":
        import winreg
        with winreg.OpenKey(winreg.HKEY_LOCAL_MACHINE, r"HARDWARE\DESCRIPTION\System\CentralProcessor\0") as key:
            processor = winreg.QueryValueEx(key, "ProcessorNameString")[0].strip()
    sources = ["src/core/DownloadEngine/DownloadEngine.cpp", "src/core/ConnectionManager/AdaptiveController.h",
        "src/core/ConnectionManager/SpeedTracker.h", "src/core/SegmentScheduler/SegmentScheduler.h",
        "src/core/FileWriter/FileWriter.cpp", "src/DownloadCli.cpp", "tests/PerformanceServer.py", "scripts/Benchmark.py",
        "src/core/ResumeManager/ResumeJournal.cpp", "src/core/ResumeManager/ResumeJournal.h",
        "src/core/ConnectionManager/RetryPolicy.h", "src/core/DownloadEngine/DownloadEngine.h",
        "src/core/FileWriter/FileWriter.h", "src/core/DownloadEngine/HttpResponse.cpp", "src/core/DownloadEngine/HttpResponse.h"]
    compiler_files = sorted((cli.parent.parent / "CMakeFiles").glob("*/CMakeCXXCompiler.cmake"))
    compiler = None
    if compiler_files:
        match = re.search(r'set\(CMAKE_CXX_COMPILER_VERSION "([^"]+)"\)', compiler_files[-1].read_text())
        if match: compiler = match[1]
    project = cli.parent.parent / "downshift_download.vcxproj"
    sdk = None
    if project.exists():
        match = re.search(r"<WindowsTargetPlatformVersion>([^<]+)</WindowsTargetPlatformVersion>", project.read_text(encoding="utf-8-sig"))
        if match: sdk = match[1]
    versions = {}
    status = ROOT / "vcpkg_installed/vcpkg/status"
    if status.exists():
        for entry in status.read_text().split("\n\n"):
            fields = dict(line.split(": ", 1) for line in entry.splitlines() if ": " in line)
            if fields.get("Package") in {"qtbase", "curl", "sqlite3", "nghttp2"} and "Version" in fields:
                versions[fields["Package"]] = fields["Version"] + ("#" + fields["Port-Version"] if "Port-Version" in fields else "")
    return {"started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(), "windows": platform.platform(),
        "processor": processor, "logical_processors": os.cpu_count(), "python": platform.python_version(),
        "msvc": compiler, "windows_sdk": sdk, "dependencies": versions,
        "git_head": subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=ROOT, text=True).strip(),
        "working_tree_changed": bool(subprocess.check_output(["git", "status", "--porcelain"], cwd=ROOT)),
        "executable_sha256": file_sha256(cli), "source_sha256": {s: file_sha256(ROOT / s) for s in sources},
        "vcpkg_baseline": json.loads((ROOT / "vcpkg.json").read_text())["builtin-baseline"],
        "configuration": cli.parent.name if cli.parent.name in {"Release", "Debug", "RelWithDebInfo", "MinSizeRel"} else "unknown",
        "size_units": "decimal MB/GB", "throughput_units": "MiB/s (1048576 bytes)",
        "network": "HTTP/1.1 over IPv4 loopback; Python server shares the machine",
        "cache": "OS caches left enabled; new native process and connections per sample; no cold-cache claim",
        "comparison": "manual modes use fixed 4 MiB chunks; automatic uses adaptive chunks unless requested otherwise",
        "idm_comparison": "not performed", "retry_recovery": "Implemented; this matrix measures fresh downloads. Interrupted/retried performance is not measured here."}


def report(document, base):
    rows = document["samples"]
    temporary = base.with_suffix(".json.tmp")
    temporary.write_text(json.dumps(document, indent=2) + "\n", encoding="utf-8")
    temporary.replace(base.with_suffix(".json"))
    if not rows: return
    with base.with_suffix(".csv").open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(rows[0]))
        writer.writeheader(); writer.writerows(rows)
    lines = ["# Downshift local benchmarks", "", f"Completed: {document['complete']}. SHA-256 verified samples: {len(rows)}.", "",
        "Sizes are decimal MB/GB; rates use MiB/s. New client process per run; OS caches stay enabled. Manual modes use fixed 4 MiB chunks; automatic uses adaptive chunks unless the run metadata says otherwise. These are controlled loopback results, not Internet or IDM comparisons.", "",
        "| Size | Server | Mode | Runs | Median client wall s | Median transfer MiB/s | Median end-to-end MiB/s | Peak client working set MiB |",
        "|---|---|---|---:|---:|---:|---:|---:|"]
    groups = {}
    for row in rows: groups.setdefault((row["size_label"], row["profile"], row["mode"]), []).append(row)
    for (size, profile, mode), samples in groups.items():
        lines.append(f"| {size} | {profile} | {mode} | {len(samples)} | {statistics.median(r['wall_seconds'] for r in samples):.3f} | "
            f"{statistics.median(r['average_bps'] / 1048576 for r in samples):.2f} | "
            f"{statistics.median(r['end_to_end_mib_s'] for r in samples):.2f} | {max(r['peak_working_set_bytes'] for r in samples) / 1048576:.2f} |")
    lines += ["", "Raw JSON/CSV include sampled peak rate, process CPU time/utilization, logical file write bytes/time, checksum/flush/allocation time, connection setup/first-byte time, range sizes, active-request limits, and tuner changes. Write timing measures Windows write calls into its cache, not physical disk throughput. Connection setup times are summed across requests and may overlap.", "",
        "Server shaping reserves 64 KiB blocks on monotonic deadlines. Per-connection cap: 4 MiB/s with 25 ms header delay; shared cap: 16 MiB/s with 25 ms header delay. Large files use only the unlimited profile by default; shaped 1/5 GB cases can be explicitly enabled. Streaming runs do not segment, irrespective of configured connection slots.", "",
        "Interrupted/retried throughput and direct IDM comparisons were not measured. HTTP/2 capability is enabled in the client, but this server speaks HTTP/1.1. A Python server on the same machine, cached writes, and loopback CPU scheduling limit generalization.", ""]
    base.with_suffix(".md").write_text("\n".join(lines), encoding="utf-8")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=pathlib.Path, default=ROOT / "build/windows-msvc/Release/DownshiftDownload.exe")
    parser.add_argument("--sizes", nargs="+", choices=SIZES, default=list(SIZES))
    parser.add_argument("--profiles", nargs="+", choices=PROFILES, default=list(PROFILES))
    parser.add_argument("--repetitions", type=int, default=3)
    parser.add_argument("--shape-large-files", action="store_true")
    parser.add_argument("--fixed-auto-chunks", action="store_true")
    parser.add_argument("--output", type=pathlib.Path)
    args = parser.parse_args()
    if args.repetitions < 1: parser.error("Repetitions must be positive.")
    cli = args.cli.resolve()
    if not cli.is_file(): parser.error("Build the Release download client first.")
    timestamp = datetime.datetime.now(datetime.timezone.utc).strftime("%Y%m%dT%H%M%SZ")
    base = (args.output or ROOT / "benchmarks/results" / ("phase3-" + timestamp)).resolve()
    base.parent.mkdir(parents=True, exist_ok=True)
    if base.with_suffix(".json").exists(): parser.error("Choose a fresh output name; existing results are preserved.")
    document = {"metadata": metadata(cli), "complete": False, "samples": [], "settings": vars(args) | {"cli": str(cli.name), "output": base.name}}
    # Keep JSON portable and free of developer-specific absolute paths.
    document["settings"]["sizes"] = list(args.sizes)
    report(document, base)
    server = ControlledServer()
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    rng = random.Random(20261012)
    try:
        scratch_parent = ROOT / "build" / "benchmark-files"
        scratch_parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix="run-", dir=scratch_parent) as temp:
            directory = pathlib.Path(temp)
            for label in args.sizes:
                size = SIZES[label]
                if shutil.disk_usage(directory).free < size + 1024 * 1024 * 1024:
                    raise RuntimeError("Insufficient free disk space for the next verified sample.")
                print(f"Preparing expected SHA-256 for {label}", flush=True)
                expected = expected_sha256(size)
                for profile in args.profiles:
                    if size > SIZES["100MB"] and profile != "unlimited" and not args.shape_large_files: continue
                    for repetition in range(1, args.repetitions + 1):
                        modes = ["1", "4", "8", "16", "auto", "stream"]
                        rng.shuffle(modes)
                        for mode in modes:
                            server.configure(size, **PROFILES[profile])
                            destination = directory / "payload.bin"
                            count = "auto" if mode == "stream" else mode
                            command = [str(cli), f"http://127.0.0.1:{server.server_port}/" + ("stream" if mode == "stream" else "range"),
                                str(destination), "--connections", count, "--sha256", expected, "--json"]
                            if mode not in ("auto", "stream") or args.fixed_auto_chunks: command += ["--chunk-kib", "4096"]
                            print(f"RUN {label} {profile} {mode} repetition {repetition}", flush=True)
                            started = time.perf_counter()
                            result = subprocess.run(command, capture_output=True, text=True, env=local_environment(), timeout=1800)
                            wall = time.perf_counter() - started
                            if result.returncode: raise RuntimeError(f"Client failed: {result.stderr}")
                            metrics = json.loads(result.stdout)
                            if destination.stat().st_size != size or file_sha256(destination) != expected:
                                raise RuntimeError("Size/checksum failure: benchmark sample rejected.")
                            if metrics["bytes"] != size or metrics["network_bytes"] != size: raise RuntimeError("Metrics byte count mismatch.")
                            ranges = sorted((r["first"], r["last"]) for r in server.records if not r["probe"])
                            if ranges[0][0] != 0 or ranges[-1][1] != size - 1 or any(a[1] + 1 != b[0] for a, b in zip(ranges, ranges[1:])):
                                raise RuntimeError("Network range overlap or gap: sample rejected.")
                            limit = 16 if count == "auto" else int(count)
                            if server.peak > limit or metrics["peak_active_ranges"] > limit: raise RuntimeError("Connection limit violated.")
                            row = {"size_label": label, "size_bytes": size, "profile": profile, "mode": mode,
                                "repetition": repetition, "sha256": expected, "sha256_verified": True, "wall_seconds": wall,
                                "end_to_end_mib_s": size / wall / 1048576, "server_peak_active": server.peak,
                                "cpu_percent_one_core": metrics["cpu_seconds"] / wall * 100,
                                "cpu_percent_all_cores": metrics["cpu_seconds"] / wall / (os.cpu_count() or 1) * 100, **metrics}
                            document["samples"].append(row)
                            report(document, base)
                            print(f"PASS {size / wall / 1048576:.2f} MiB/s end-to-end, {metrics['average_bps'] / 1048576:.2f} MiB/s transfer", flush=True)
                            destination.unlink()
        document["complete"] = True
    finally:
        server.shutdown(); server.server_close(); thread.join(2)
        document["metadata"]["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
        report(document, base)
    print(f"{len(document['samples'])} verified samples saved to {base.with_suffix('.json')}", flush=True)


if __name__ == "__main__": main()
