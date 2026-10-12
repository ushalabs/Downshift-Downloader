"""Bounded-memory deterministic loopback server for Phase 3 tests and benchmarks."""
from __future__ import annotations
import hashlib
import http.server
import os
import re
import threading
import time

BLOCK = bytes(range(256)) * 4096


def expected_sha256(size):
    digest = hashlib.sha256()
    full, tail = divmod(size, len(BLOCK))
    for _ in range(full): digest.update(BLOCK)
    digest.update(BLOCK[:tail])
    return digest.hexdigest()


def file_sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as stream:
        while block := stream.read(1024 * 1024): digest.update(block)
    return digest.hexdigest()


def local_environment():
    environment = {key: value for key, value in os.environ.items() if not key.lower().endswith("_proxy")}
    environment["no_proxy"] = "localhost,127.0.0.1"
    return environment


class ControlledServer(http.server.ThreadingHTTPServer):
    daemon_threads = True
    request_queue_size = 32
    def __init__(self):
        super().__init__(("127.0.0.1", 0), Handler)
        self.lock = threading.Lock()
        self.configure(1)

    def configure(self, size, per_connection_bps=0, total_bps=0, header_delay=0, heterogeneous=False, weak_etag=False):
        with self.lock:
            if getattr(self, "active", 0): raise RuntimeError("Server still has active transfers.")
            self.size = size
            self.per_connection_bps, self.total_bps = per_connection_bps, total_bps
            self.header_delay, self.heterogeneous = header_delay, heterogeneous
            self.weak_etag = weak_etag
            self.records = []
            self.active = self.peak = self.next_connection = 0
            self.global_deadline = time.perf_counter()

    def pace(self, count, local_deadline, connection):
        now = time.perf_counter()
        rate = self.per_connection_bps
        if self.heterogeneous and connection % 4 == 0: rate /= 8
        if rate: local_deadline = max(now, local_deadline) + count / rate
        with self.lock:
            global_deadline = now
            if self.total_bps:
                self.global_deadline = max(now, self.global_deadline) + count / self.total_bps
                global_deadline = self.global_deadline
        delay = max(local_deadline, global_deadline) - now
        if delay > 0: time.sleep(delay)
        return local_deadline


class Handler(http.server.BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"
    def setup(self):
        super().setup()
        with self.server.lock:
            self.connection_id = self.server.next_connection
            self.server.next_connection += 1

    def log_message(self, *_):
        pass

    def handle(self):
        try: super().handle()
        except ConnectionError: pass

    def do_GET(self):
        size = self.server.size
        raw_range = self.headers.get("Range")
        probe = raw_range == "bytes=0-0" and not self.headers.get("If-Match")
        match = re.fullmatch(r"bytes=(\d+)-(\d+)", raw_range or "")
        segmented = self.path.split("?", 1)[0] == "/range" and match is not None
        first, last = tuple(map(int, match.groups())) if segmented else (0, size - 1)
        if first < 0 or last >= size or last < first:
            self.send_error(416); return
        with self.server.lock:
            self.server.records.append({"first": first, "last": last, "probe": probe,
                "ranged": segmented, "if_match": self.headers.get("If-Match"), "connection": self.connection_id})
            if not probe:
                self.server.active += 1
                self.server.peak = max(self.server.peak, self.server.active)
        try:
            if self.server.header_delay: time.sleep(self.server.header_delay)
            self.send_response(206 if segmented else 200)
            self.send_header("Content-Length", str(last - first + 1))
            self.send_header("ETag", ("W/" if self.server.weak_etag else "") + f'"pattern-{size}"')
            if segmented: self.send_header("Content-Range", f"bytes {first}-{last}/{size}")
            self.end_headers()
            local_deadline = time.perf_counter()
            for position in range(first, last + 1, 65536):
                count = min(65536, last + 1 - position)
                if not probe: local_deadline = self.server.pace(count, local_deadline, self.connection_id)
                offset = position % 256
                self.wfile.write(BLOCK[offset:offset + count])
            self.wfile.flush()
        except ConnectionError:
            pass
        finally:
            with self.server.lock:
                if not probe: self.server.active -= 1
