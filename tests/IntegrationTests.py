"""Deterministic loopback HTTP/HTTPS tests; no public network or trusted-store changes."""
from __future__ import annotations

import hashlib
import http.server
import os
import pathlib
import re
import select
import socket
import ssl
import subprocess
import sys
import tempfile
import threading
import time
from collections import defaultdict
from http.server import ThreadingHTTPServer

SMALL = bytes(range(256)) * 2048 + b"Downshift-tail"
LARGE = bytes(range(256)) * (17 * 1024 * 1024 // 256) + b"Downshift-large-tail"
ETAG = '"fixture-version-1"'


class Tunnel(http.server.BaseHTTPRequestHandler):
    def log_message(self, *_):
        pass

    def do_CONNECT(self):
        # A deliberately restricted test proxy: its only target is our TLS fixture.
        if self.path != f"localhost:{self.server.target_port}":
            self.send_error(403)
            return
        self.server.tunnels += 1
        with socket.create_connection(("127.0.0.1", self.server.target_port), timeout=5) as upstream:
            self.send_response(200, "Connection established")
            self.end_headers()
            sockets = (self.connection, upstream)
            try:
                while True:
                    ready, _, _ = select.select(sockets, [], [], 5)
                    if not ready: return
                    for source in ready:
                        data = source.recv(65536)
                        if not data: return
                        (upstream if source is self.connection else self.connection).sendall(data)
            except (ConnectionError, socket.timeout):
                pass


class Fixture(http.server.ThreadingHTTPServer):
    daemon_threads = True
    def __init__(self):
        super().__init__(("127.0.0.1", 0), Handler)
        self.lock = threading.Lock()
        self.requests = defaultdict(list)
        self.active = defaultdict(int)
        self.peak = defaultdict(int)
        self.first_completed = threading.Event()
        self.http_port = 0


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def handle(self):
        try:
            super().handle()
        except (ConnectionError, ssl.SSLError):
            pass  # Intentional client aborts can also occur between keep-alive requests.

    def log_message(self, *_):
        pass

    def do_GET(self):
        try:
            self.respond()
        except (BrokenPipeError, ConnectionResetError, ConnectionAbortedError, ssl.SSLError):
            pass  # Expected when the engine rejects headers or cancels other ranges.

    def headers_for(self, status, length=None, extra=()):
        self.send_response(status)
        if length is not None:
            self.send_header("Content-Length", str(length))
        for key, value in extra:
            self.send_header(key, value)
        self.end_headers()

    def chunked(self, body, trailer=False):
        for position in range(0, len(body), 16384):
            block = body[position:position + 16384]
            self.wfile.write(f"{len(block):x}\r\n".encode() + block + b"\r\n")
        self.wfile.write(b"0\r\n" + (b'ETag: "evil-trailer"\r\nContent-Length: 999\r\n' if trailer else b"") + b"\r\n")

    def respond(self):
        case = self.path.split("?", 1)[0].strip("/")
        raw_range = self.headers.get("Range")
        with self.server.lock:
            self.server.requests[case].append((raw_range, self.headers.get("If-Match"), self.headers.get("Accept-Encoding")))
        if case.startswith("status-"):
            self.headers_for(int(case[7:]), 0)
            return
        if case in {"redirect", "redirect-loop", "redirect-ftp", "redirect-user", "downgrade"}:
            location = {"redirect": "/range", "redirect-loop": "/redirect-loop",
                        "redirect-ftp": "ftp://127.0.0.1/file", "redirect-user": f"http://secret:password@127.0.0.1:{self.server.server_port}/range",
                        "downgrade": f"http://127.0.0.1:{self.server.http_port}/range"}[case]
            self.headers_for(302, 8, [("Location", location)])
            self.wfile.write(b"redirect")
            return
        if case == "stall":
            self.headers_for(200, len(SMALL))
            time.sleep(1)
            self.wfile.write(SMALL)
            return
        if case == "cancel":
            self.headers_for(200, len(LARGE))
            for start in range(0, len(LARGE), 4096):
                self.wfile.write(LARGE[start:start + 4096]); self.wfile.flush(); time.sleep(0.02)
            return
        if case == "disconnect":
            self.connection.shutdown(socket.SHUT_RDWR)
            self.connection.close()
            return
        body = LARGE if case in {"range", "unadvertised", "fallback", "wrong-range", "wrong-total", "changed", "changed-200",
                                 "short-range", "long-range", "missing-range", "precondition", "segment-416", "segment-redirect"} else SMALL
        if case in {"empty", "empty-416"}:
            body = b""
        if case == "one-byte":
            body = b"X"
        etag = "W/" + ETAG if case == "weak-etag" else ETAG
        common = [] if case == "no-etag" else [("ETag", etag)]
        common += [("Content-Disposition", "attachment; filename*=UTF-8''caf%C3%A9.bin")]
        if case != "unadvertised":
            common += [("Accept-Ranges", "bytes")]
        range_match = re.fullmatch(r"bytes=(\d+)-(\d+)", raw_range or "")
        probe = raw_range == "bytes=0-0" and self.headers.get("If-Match") is None
        ignore_range = case in {"ignore", "unknown-length", "chunked", "compressed", "short-stream", "duplicate-length", "ambiguous"}
        if case == "empty-416" and probe:
            self.headers_for(416, 0, [("Content-Range", "bytes */0")]); return
        if case == "interim":
            self.wfile.write(b"HTTP/1.1 103 Early Hints\r\nLink: </style.css>; rel=preload\r\n\r\n")
        if case == "fallback" and range_match and not probe and int(range_match[1]) > 0:
            if not self.server.first_completed.wait(3):
                raise RuntimeError("First range did not complete before fallback.")
            self.headers_for(200, len(body), common); self.wfile.write(body); return
        if range_match and not ignore_range and body:
            first, last = map(int, range_match.groups())
            payload = body[first:last + 1]
            status = 206
            if not probe:
                if case == "precondition": self.headers_for(412, 0); return
                if case == "segment-416": self.headers_for(416, 0, [("Content-Range", f"bytes */{len(body)}")]); return
                if case == "segment-redirect": self.headers_for(302, 0, [("Location", "/range")]); return
                if case == "changed-200":
                    self.headers_for(200, len(body), [("ETag", '"new-version"')]); self.wfile.write(body); return
                if case == "changed": common = [(k, '"new-version"' if k == "ETag" else v) for k, v in common]
            response_first = first + 1 if case == "wrong-range" and not probe else first
            total = len(body) + 1 if case == "wrong-total" and not probe else len(body)
            if case == "bad-probe" and probe:
                response_first = 1
            if not (case == "missing-range" and not probe):
                common += [("Content-Range", f"bytes {response_first}-{last}/{total}")]
            if case == "long-range" and not probe:
                self.headers_for(status, None, common + [("Transfer-Encoding", "chunked")])
                self.chunked(payload + b"EXTRA"); return
            self.headers_for(status, len(payload), common)
            with self.server.lock:
                if not probe:
                    self.server.active[case] += 1
                    self.server.peak[case] = max(self.server.peak[case], self.server.active[case])
            try:
                if not probe: time.sleep(0.04)  # Ensure simultaneous ranges are observable.
                if case == "short-range" and not probe:
                    self.wfile.write(payload[:len(payload) // 2]); self.close_connection = True
                else:
                    self.wfile.write(payload)
                    self.wfile.flush()
                if case == "fallback" and not probe and first == 0: self.server.first_completed.set()
            finally:
                with self.server.lock:
                    if not probe: self.server.active[case] -= 1
            return
        if case == "unknown-length":
            self.headers_for(200, None, common + [("Connection", "close")])
            self.wfile.write(body); self.close_connection = True
        elif case == "chunked":
            self.headers_for(200, None, common + [("Transfer-Encoding", "chunked")])
            self.chunked(body, True)
        elif case == "compressed":
            self.headers_for(200, len(body), common + [("Content-Encoding", "gzip")]); self.wfile.write(body)
        elif case == "duplicate-length":
            self.headers_for(200, len(body), common + [("Content-Length", str(len(body)))]); self.wfile.write(body)
        elif case == "ambiguous":
            self.headers_for(200, len(body), common + [("Transfer-Encoding", "chunked")]); self.chunked(body)
        elif case == "short-stream":
            self.headers_for(200, len(body), common); self.wfile.write(body[:100]); self.close_connection = True
        else:
            self.headers_for(200, len(body), common); self.wfile.write(body)


def main():
    cli, unit, fixtures = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3])
    http = Fixture()
    https = Fixture()
    https.http_port = http.server_port
    context = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    context.load_cert_chain(fixtures / "localhost-cert.pem", fixtures / "localhost-key.pem")
    https.socket = context.wrap_socket(https.socket, server_side=True)
    proxy = ThreadingHTTPServer(("127.0.0.1", 0), Tunnel)
    proxy.daemon_threads = True
    proxy.target_port = https.server_port
    proxy.tunnels = 0
    threads = [threading.Thread(target=server.serve_forever, daemon=True) for server in (http, https, proxy)]
    for thread in threads: thread.start()
    count = 0
    local_environment = {key: value for key, value in os.environ.items() if not key.lower().endswith("_proxy")}
    local_environment["no_proxy"] = "localhost,127.0.0.1"
    try:
        with tempfile.TemporaryDirectory(prefix="integration-", dir=pathlib.Path.cwd()) as temp:
            directory = pathlib.Path(temp)
            def run(case, body=SMALL, mode="segmented", success=True, extra=(), url=None, checksum=True, environment=None):
                nonlocal count
                count += 1
                output = directory / f"{count}-{case}.bin"
                command = [str(cli), url or f"http://127.0.0.1:{http.server_port}/{case}", str(output), "--connections", "4", "--chunk-kib", "4096", *extra]
                if checksum: command += ["--sha256", hashlib.sha256(body).hexdigest().upper()]
                result = subprocess.run(command, capture_output=True, text=True, timeout=15, env=local_environment if environment is None else environment)
                if success:
                    assert result.returncode == 0, f"{case}: {result.stderr}"
                    assert output.read_bytes() == body, f"{case}: incorrect completed file"
                    assert f"mode={mode}" in result.stdout, f"{case}: {result.stdout}"
                    assert not output.with_name(output.name + ".part").exists(), f"{case}: leftover partial"
                else:
                    assert result.returncode != 0, f"{case}: unsafe response accepted"
                    assert not output.exists(), f"{case}: failed transfer finalized"
                    assert "secret" not in result.stderr and "password" not in result.stderr, "Credentials exposed"
                print(f"PASS {case}", flush=True)
                return result, output

            for case in ("range", "unadvertised"):
                run(case, LARGE)
                requests = http.requests[case]
                ranges = sorted(tuple(map(int, re.fullmatch(r"bytes=(\d+)-(\d+)", r).groups())) for r, match, _ in requests if match)
                assert len(ranges) == 5 and ranges[0][0] == 0 and ranges[-1][1] == len(LARGE) - 1
                assert all(ranges[i][1] + 1 == ranges[i + 1][0] for i in range(len(ranges) - 1)), "Overlapping or missing network ranges"
                assert all(match == ETAG and encoding == "identity" for _, match, encoding in requests if match)
                assert 2 <= http.peak[case] <= 4, "Concurrency or connection limit incorrect"
            result, _ = run("fallback", LARGE, "streaming")
            assert "fallback=yes" in result.stdout and "ranges=0" in result.stdout
            assert http.first_completed.is_set(), "Fallback did not follow an already written range"
            assert any(r is None and match == ETAG for r, match, _ in http.requests["fallback"]), "Fallback was not conditional"
            for case in ("ignore", "no-etag", "weak-etag", "bad-probe", "unknown-length", "chunked"):
                run(case, mode="streaming")
            for case in ("empty", "empty-416"):
                run(case, b"", "streaming")
            run("one-byte", b"X")
            run("interim")
            run("redirect", LARGE)
            for case in ("wrong-range", "wrong-total", "changed", "changed-200", "short-range", "long-range",
                         "missing-range", "precondition", "segment-416", "segment-redirect", "compressed", "short-stream",
                         "duplicate-length", "ambiguous", "redirect-loop", "redirect-ftp", "redirect-user", "disconnect"):
                run(case, success=False, checksum=False)
            for status in (401, 403, 404, 429, 500, 503): run(f"status-{status}", success=False, checksum=False)
            result, partial = run("stall", mode="streaming", success=False, checksum=False, extra=("--timeout-ms", "200"))
            assert "category=5" in result.stderr, f"Timeout category incorrect: {result.stderr}"
            run("ignore", success=False, checksum=False, extra=("--sha256", "0" * 64))
            run("invalid-hash", success=False, checksum=False, extra=("--sha256", "bad"))
            run("invalid-scheme", success=False, checksum=False, url="file:///C:/Windows/win.ini")
            run("credentials", success=False, checksum=False, url=f"http://secret:password@127.0.0.1:{http.server_port}/range")
            secure = f"https://localhost:{https.server_port}"
            ca = ("--ca-bundle", str(fixtures / "test-ca.pem"))
            run("range", LARGE, url=secure + "/range", extra=ca)
            run("ignore", mode="streaming", url=secure + "/ignore", extra=ca)
            proxy_environment = dict(os.environ)
            for key in tuple(proxy_environment):
                if key.lower().endswith("_proxy"): proxy_environment.pop(key)
            proxy_environment.update(https_proxy=f"http://127.0.0.1:{proxy.server_port}", no_proxy="")
            run("https-connect-proxy", LARGE, url=secure + "/range", extra=ca, environment=proxy_environment)
            assert proxy.tunnels >= 1, "HTTPS CONNECT proxy was bypassed"
            result, _ = run("untrusted-ca", success=False, checksum=False, url=secure + "/range")
            assert "category=6" in result.stderr, "Untrusted certificate was not rejected by TLS"
            result, _ = run("hostname-mismatch", success=False, checksum=False,
                url=f"https://127.0.0.1:{https.server_port}/range", extra=ca)
            assert "category=6" in result.stderr, "Incorrect hostname was not rejected by TLS"
            run("downgrade", success=False, checksum=False, url=secure + "/downgrade", extra=ca)
            protected = directory / "existing.bin"
            protected.write_bytes(b"KEEP")
            result = subprocess.run([str(cli), f"http://127.0.0.1:{http.server_port}/range", str(protected)], capture_output=True, timeout=10, env=local_environment)
            assert result.returncode != 0 and protected.read_bytes() == b"KEEP"
            count += 1; print("PASS preserve-existing-destination", flush=True)
            protected.unlink()
            protected.with_name("existing.bin.part").write_bytes(b"KEEP-PART")
            result = subprocess.run([str(cli), f"http://127.0.0.1:{http.server_port}/range", str(protected)], capture_output=True, timeout=10, env=local_environment)
            assert result.returncode != 0 and protected.with_name("existing.bin.part").read_bytes() == b"KEEP-PART"
            count += 1; print("PASS preserve-existing-partial", flush=True)
            cancelled = directory / "cancel.bin"
            result = subprocess.run([str(unit), "--cancel", f"http://127.0.0.1:{http.server_port}/cancel", str(cancelled)], capture_output=True, text=True, timeout=10, env=local_environment)
            assert result.returncode == 0 and not cancelled.exists(), f"Cancellation failed: {result.stderr}"
            count += 1; print("PASS cooperative-cancellation", flush=True)
    finally:
        for server in (http, https, proxy): server.shutdown(); server.server_close()
        for thread in threads: thread.join(2)
    print(f"{count} loopback HTTP/HTTPS scenarios passed.", flush=True)


if __name__ == "__main__":
    main()
