"""Real cross-process pause/crash recovery and bounded retry acceptance checks."""
import collections
import http.server
import json
import os
import pathlib
import shutil
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time

from PerformanceServer import BLOCK, expected_sha256, file_sha256, local_environment


class Server(http.server.ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 32

    def configure(self, **settings):
        deadline = time.monotonic() + 5
        while self.active and time.monotonic() < deadline:
            time.sleep(.02)
        assert self.active == 0, "Previous fixture requests did not drain"
        self.settings = dict(size=12 * 1048576, etag='"version-1"', modified="Mon, 12 Oct 2026 00:00:00 GMT",
            bps=4 * 1048576, fault="", scope="range", status=503, retry_after="", ignore=False, weak=False,
            fault_first=None, change_after_probe=False, drop_bytes=None) | settings
        self.records = []
        self.attempts = collections.Counter()


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def log_message(self, *_):
        pass

    def handle(self):
        try:
            super().handle()
        except (ConnectionError, OSError):
            pass

    def do_GET(self):
        with self.server.lock:
            self.server.active += 1
        try:
            self.transfer()
        except (ConnectionError, OSError):
            pass
        finally:
            with self.server.lock:
                self.server.active -= 1

    def transfer(self):
        s = self.server.settings.copy()
        raw = self.headers.get("Range")
        probe = raw == "bytes=0-0"
        ranged = bool(raw) and self.path.startswith("/range") and not s["ignore"]
        first, last = map(int, raw[6:].split("-")) if ranged else (0, s["size"] - 1)
        key = (self.path, first, last, probe)
        with self.server.lock:
            attempt = self.server.attempts[key]
            self.server.attempts[key] += 1
            self.server.records.append(dict(first=first, last=last, probe=probe, ranged=ranged,
                if_match=self.headers.get("If-Match"), time=time.monotonic(), attempt=attempt))
        scope = "probe" if probe else "range" if ranged else "stream"
        fault = s["fault"] if s["scope"] in (scope, "all") else ""
        if s["fault_first"] is not None and first != s["fault_first"]: fault = ""
        failing = fault.endswith("always") or (fault.endswith("once") and attempt == 0)
        if self.headers.get("If-Match") and self.headers["If-Match"] != s["etag"]:
            self.send_response(412); self.send_header("Content-Length", "0"); self.end_headers(); return
        if fault.startswith("status") and failing:
            self.send_response(s["status"]); self.send_header("Content-Length", "0")
            if s["retry_after"]: self.send_header("Retry-After", s["retry_after"])
            self.end_headers(); return
        if fault.startswith("stall") and failing:
            time.sleep(.4)
        self.send_response(206 if ranged else 200)
        self.send_header("Content-Length", str(last - first + 1))
        self.send_header("ETag", ("W/" if s["weak"] else "") + s["etag"])
        self.send_header("Last-Modified", s["modified"])
        if ranged:
            self.send_header("Content-Range", f"bytes {first}-{last}/{s['size']}")
        self.end_headers()
        end = first + (last - first + 1) // 2 - 1 if fault.startswith("drop") and failing else last
        if fault.startswith("drop") and failing and s["drop_bytes"] is not None:
            end = min(end, first + s["drop_bytes"] - 1)
        deadline = time.monotonic()
        for pos in range(first, end + 1, 65536):
            count = min(65536, end + 1 - pos)
            if not probe and s["bps"]:
                deadline += count / s["bps"]
                time.sleep(max(0, deadline - time.monotonic()))
            offset = pos % 256
            self.wfile.write(BLOCK[offset:offset + count])
        self.wfile.flush()
        if probe and s["change_after_probe"]:
            self.server.settings["etag"] = '"version-2"'
        if end != last:
            self.close_connection = True
            self.connection.shutdown(socket.SHUT_RDWR)


def main():
    cli, unit = map(pathlib.Path, sys.argv[1:3])
    server = Server(("127.0.0.1", 0), Handler)
    server.lock = threading.Lock(); server.active = 0
    thread = threading.Thread(target=server.serve_forever, daemon=True); thread.start()
    count = 0
    try:
        with tempfile.TemporaryDirectory(prefix="recovery-test-", dir=pathlib.Path.cwd()) as temp:
            directory = pathlib.Path(temp)

            def url(path="range"):
                return f"http://127.0.0.1:{server.server_port}/{path}"

            def command(output, *extra, path="range"):
                return [str(cli), url(path), str(output), "--connections", "4", "--chunk-kib", "1024",
                    "--json", "--progress-json", *extra]

            def run(output, *extra, path="range", expected=0, override=None):
                cmd = command(output, *extra, path=path)
                if override: cmd[1] = override
                result = subprocess.run(cmd, capture_output=True, text=True,
                    env=local_environment(), timeout=35)
                assert result.returncode == expected, f"Unexpected exit {result.returncode}: {result.stderr}"
                if expected == 0:
                    size = server.settings["size"]
                    assert output.stat().st_size == size and file_sha256(output) == expected_sha256(size)
                    assert not pathlib.Path(str(output) + ".part").exists()
                    assert not pathlib.Path(str(output) + ".part.resume").exists()
                    return json.loads(result.stdout)
                assert not output.exists()
                return result

            def inspect(output):
                result = subprocess.run([str(unit), "--inspect-recovery", str(output)], capture_output=True, text=True, timeout=5)
                assert result.returncode == 0, result.stderr
                return json.loads(result.stdout)

            def pause(name, path="range", **settings):
                server.configure(**settings)
                output = directory / (name + ".bin")
                run(output, "--sha256", expected_sha256(server.settings["size"]), "--pause-after-mib", "2", path=path, expected=3)
                record = inspect(output)
                assert record["status"] == 1 and (record["ranges"] if record["resumable"] else True)
                return output, record

            def pass_case(name):
                nonlocal count
                count += 1; print(f"PASS {name}", flush=True)

            def no_reused_ranges(record, records):
                fresh = [(r["first"], r["last"]) for r in records if not r["probe"]]
                assert all(b < first or a > last for a, b in fresh for first, last in record["ranges"]), "Saved range fetched again"
                coverage = sorted(fresh + [tuple(r) for r in record["ranges"]])
                assert coverage[0][0] == 0 and coverage[-1][1] == record["total"] - 1
                assert all(a[1] + 1 == b[0] for a, b in zip(coverage, coverage[1:])), "Recovery gap/overlap"

            output, record = pause("pause-resume")
            run(output, expected=1)  # Starting fresh never implicitly overwrites a saved task.
            server.configure()
            metrics = run(output, "--resume", "--connections", "auto")
            assert metrics["recovered_bytes"] == sum(b - a + 1 for a, b in record["ranges"])
            assert metrics["network_bytes"] == metrics["bytes"] - metrics["recovered_bytes"]
            no_reused_ranges(record, server.records)
            assert all(r["if_match"] == '"version-1"' for r in server.records)
            pass_case("pause-resume-cross-process")

            output, record = pause("repeated-pause")
            server.configure()
            run(output, "--resume", "--pause-after-mib", "8", expected=3)
            second = inspect(output)
            assert sum(b - a + 1 for a, b in second["ranges"]) > sum(b - a + 1 for a, b in record["ranges"])
            server.configure()
            run(output, "--resume")
            no_reused_ranges(second, server.records)
            pass_case("repeated-pause-resume")

            server.configure(size=32 * 1048576)
            output = directory / "crash.bin"
            child = subprocess.Popen(command(output), stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, env=local_environment())
            try:
                for line in child.stderr:
                    snapshot = json.loads(line)
                    if snapshot["verified_bytes"] >= 1048576:
                        competing = subprocess.run(command(output, "--resume"), capture_output=True, text=True, timeout=5)
                        assert competing.returncode != 0 and "Another worker" in competing.stderr
                        child.kill(); break
                child.communicate(timeout=5)
                assert child.returncode != 0 and not output.exists()
            finally:
                if child.poll() is None: child.kill(); child.wait()
            record = inspect(output)
            assert record["ranges"] and record["status"] == 0
            server.configure(size=32 * 1048576, bps=16 * 1048576)
            metrics = run(output, "--resume")
            assert metrics["recovered_bytes"] > 0
            no_reused_ranges(record, server.records)
            pass_case("forced-process-termination-recovery")

            for name, changes in [("changed-etag", dict(etag='"version-2"')), ("changed-size", dict(size=13 * 1048576)),
                    ("changed-modified", dict(modified="Tue, 13 Oct 2026 00:00:00 GMT")), ("lost-ranges", dict(ignore=True))]:
                output, _ = pause(name)
                partial = pathlib.Path(str(output) + ".part"); before = file_sha256(partial)
                metadata = pathlib.Path(str(output) + ".part.resume"); old_metadata = metadata.read_bytes()
                server.configure(**changes)
                run(output, "--resume", expected=1)
                assert file_sha256(partial) == before and metadata.read_bytes() == old_metadata
                assert all(r["probe"] for r in server.records)
                pass_case(name + "-refused")

            output, record = pause("changed-during-resume")
            partial = pathlib.Path(str(output) + ".part"); before = file_sha256(partial)
            server.configure(change_after_probe=True)
            run(output, "--resume", expected=1)
            assert file_sha256(partial) == before and inspect(output)["ranges"] == record["ranges"]
            pass_case("changed-resource-during-resume-refused")

            for name in ("corrupt-bytes", "replaced-file", "truncated-file", "hard-linked-file", "corrupt-ledger", "wrong-url", "wrong-hash"):
                output, record = pause(name)
                partial = pathlib.Path(str(output) + ".part")
                ledger = pathlib.Path(str(output) + ".part.resume")
                if name == "corrupt-bytes":
                    with partial.open("r+b") as f: f.seek(record["ranges"][0][0]); f.write(b"X")
                elif name == "replaced-file":
                    original = directory / (name + ".old"); partial.rename(original); shutil.copyfile(original, partial)
                elif name == "truncated-file":
                    with partial.open("r+b") as f: f.truncate(100)
                elif name == "hard-linked-file":
                    os.link(partial, directory / (name + ".link"))
                elif name == "corrupt-ledger":
                    with ledger.open("r+b") as f: f.write(b"BROKEN")
                before, old_ledger = file_sha256(partial), ledger.read_bytes()
                server.configure()
                extra = ("--sha256", "0" * 64) if name == "wrong-hash" else ()
                run(output, "--resume", *extra, path="range?different=1" if name == "wrong-url" else "range", expected=1)
                assert file_sha256(partial) == before and ledger.read_bytes() == old_ledger and not server.records
                if name == "corrupt-ledger":
                    run(output, "--restart")
                    archived = list(directory.glob("Downshift.previous-*.part"))
                    assert any(file_sha256(p) == before for p in archived)
                pass_case(name + "-refused")

            output, record = pause("stream-pause", path="stream")
            assert not record["resumable"]
            server.configure()
            run(output, "--resume", path="stream", expected=1)
            assert not server.records
            run(output, "--restart", path="stream")
            pass_case("stream-pause-requires-explicit-restart")

            server.configure(size=4 * 1048576, bps=0)
            output = directory / "complete-checkpoint.bin"
            result = subprocess.run([str(unit), "--pause-finalizing", url(), str(output)], capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            record = inspect(output)
            assert sum(b - a + 1 for a, b in record["ranges"]) == server.settings["size"]
            server.configure(size=4 * 1048576, bps=0)
            metrics = run(output, "--resume")
            assert metrics["network_bytes"] == 0 and metrics["recovered_bytes"] == metrics["bytes"]
            assert all(r["probe"] for r in server.records)
            pass_case("complete-checkpoint-finalizes-without-refetch")

            server.configure(size=4 * 1048576, bps=0)
            output = directory / "renamed-checkpoint.bin"
            result = subprocess.run([str(unit), "--pause-finalizing", url(), str(output)], capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            result = subprocess.run([str(unit), "--rename-checkpoint", str(output)], capture_output=True, timeout=10)
            assert result.returncode == 0 and output.exists(), result.stderr
            server.configure(size=4 * 1048576, bps=0, etag='"new-remote-version"')
            metrics = run(output, "--resume")
            assert metrics["network_bytes"] == 0 and metrics["recovered_bytes"] == metrics["bytes"] and not server.records
            pass_case("post-rename-crash-reconciliation")

            server.configure(size=4 * 1048576, bps=0)
            output = directory / "renamed-corrupt.bin"
            result = subprocess.run([str(unit), "--pause-finalizing", url(), str(output)], capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            result = subprocess.run([str(unit), "--rename-checkpoint", str(output)], capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            with output.open("r+b") as f: f.write(b"X")
            before = file_sha256(output)
            result = subprocess.run(command(output, "--resume"), capture_output=True, text=True, timeout=10)
            assert result.returncode != 0 and file_sha256(output) == before
            pass_case("post-rename-corruption-refused")

            server.configure(size=4 * 1048576, bps=0)
            output = directory / "renamed-stream.bin"
            result = subprocess.run([str(unit), "--pause-finalizing", url("stream"), str(output)], capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            result = subprocess.run([str(unit), "--rename-checkpoint", str(output)], capture_output=True, timeout=10)
            assert result.returncode == 0, result.stderr
            server.configure(size=4 * 1048576, bps=0)
            metrics = run(output, "--resume", path="stream")
            assert metrics["network_bytes"] == 0 and not server.records
            pass_case("stream-post-rename-reconciliation")

            server.configure(size=4 * 1048576, bps=0)
            output = directory / "legacy-partial.bin"
            pathlib.Path(str(output) + ".part").write_bytes(b"OLD_PHASE_2_PARTIAL")
            run(output, "--resume", expected=1)
            assert pathlib.Path(str(output) + ".part").read_bytes() == b"OLD_PHASE_2_PARTIAL"
            run(output, "--restart")
            assert any(p.read_bytes() == b"OLD_PHASE_2_PARTIAL" for p in directory.glob("Downshift.previous-*.part"))
            pass_case("legacy-partial-preserved-and-restarted")

            for scope, fault in [("range", "status-once"), ("range", "drop-once"), ("probe", "status-once"),
                    ("probe", "stall-once"), ("stream", "drop-once")]:
                server.configure(size=4 * 1048576, bps=0, scope=scope, fault=fault)
                output = directory / (scope + "-" + fault + ".bin")
                args = ("--timeout-ms", "150") if fault.startswith("stall") else ()
                metrics = run(output, *args, path="stream" if scope == "stream" else "range")
                assert metrics["retries"] > 0
                if fault.startswith("drop"): assert metrics["network_bytes"] > metrics["bytes"]
                pass_case(scope + "-" + fault + "-retried")

            server.configure(size=12 * 1048576, bps=0, fault="status-always", scope="range", fault_first=0)
            output = directory / "retry-exhausted-partial.bin"
            run(output, "--retries", "1", expected=1)
            record = inspect(output)
            assert record["ranges"] and record["status"] == 2
            server.configure(size=12 * 1048576, bps=0)
            run(output, "--resume")
            no_reused_ranges(record, server.records)
            pass_case("retry-exhaustion-retains-resumable-work")

            server.configure(size=1048576, bps=0, fault="status-once", scope="probe", status=429, retry_after="1")
            run(directory / "retry-after.bin")
            assert server.records[1]["time"] - server.records[0]["time"] >= .95
            pass_case("retry-after-respected")

            for name, status, after, retries, requests in [("exhaustion", 503, "", "2", 3), ("excessive-retry-after", 503, "1000", "3", 1),
                    ("unauthorized", 401, "", "3", 1), ("forbidden", 403, "", "3", 1), ("not-found", 404, "", "3", 1), ("precondition", 412, "", "3", 1)]:
                server.configure(size=1048576, bps=0, fault="status-always", scope="probe", status=status, retry_after=after)
                run(directory / (name + ".bin"), "--retries", retries, expected=1)
                assert len(server.records) == requests
                pass_case(name + "-bounded")

            server.configure(fault="status-always", scope="probe", retry_after="30")
            output = directory / "pause-backoff.bin"
            start = time.monotonic()
            result = subprocess.run([str(unit), "--pause-timer", url(), str(output)], capture_output=True, timeout=5)
            assert result.returncode == 0 and time.monotonic() - start < 2, result.stderr
            assert inspect(output)["status"] == 1
            pass_case("pause-interrupts-backoff")

            # Certificate failures must fail immediately with the normal retry default.
            tls = Server(("127.0.0.1", 0), Handler)
            tls.lock = threading.Lock(); tls.active = 0; tls.configure(size=1048576, bps=0)
            fixtures = pathlib.Path(__file__).parent / "fixtures"
            context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
            context.load_cert_chain(fixtures / "localhost-cert.pem", fixtures / "localhost-key.pem")
            tls.socket = context.wrap_socket(tls.socket, server_side=True)
            tls_thread = threading.Thread(target=tls.serve_forever, daemon=True); tls_thread.start()
            try:
                start = time.monotonic()
                run(directory / "untrusted.bin", override=f"https://127.0.0.1:{tls.server_port}/range", expected=1)
                assert not tls.records and time.monotonic() - start < 2
                pass_case("tls-failure-not-retried")
            finally:
                tls.shutdown(); tls.server_close(); tls_thread.join(2)
    finally:
        server.shutdown(); server.server_close(); thread.join(2)
    print(f"{count} recovery scenarios passed.", flush=True)


if __name__ == "__main__":
    main()
