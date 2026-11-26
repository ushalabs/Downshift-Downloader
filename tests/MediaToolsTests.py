"""Extractor upgrade, verification failure, offline and daily-cache regressions.

Only private copied installer scripts use the mock release provider. Downloaded
fixture bytes are never executed. Production discovery is tested separately.
"""
import hashlib
import http.server
import json
import pathlib
import shutil
import subprocess
import sys
import tempfile
import threading

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHELL = sys.argv[1]
sha = lambda data: hashlib.sha256(data).hexdigest()
payload = b'verified new extractor fixture' * 4096

class Server(http.server.BaseHTTPRequestHandler):
    def log_message(self, *args):
        pass

    def do_GET(self):
        self.send_response(200)
        self.send_header('Content-Length', str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

server = http.server.ThreadingHTTPServer(('127.0.0.1', 0), Server)
threading.Thread(target=server.serve_forever, daemon=True).start()
checks = 0
try:
    with tempfile.TemporaryDirectory(prefix='downshift-extractor-') as directory:
        root = pathlib.Path(directory)
        scripts = root / 'scripts'
        scripts.mkdir()
        installer = scripts / 'Install-MediaTools.ps1'
        shutil.copy2(ROOT / 'scripts/Install-MediaTools.ps1', installer)
        old = b'previous verified extractor'
        lock = json.loads((ROOT / 'scripts/media-tools-lock.json').read_text())
        for item in lock['files'] + lock['assets']:
            if item['name'] == 'yt-dlp.exe':
                item['sha256'] = sha(old)
        lock_bytes = json.dumps(lock).encode()
        (scripts / 'media-tools-lock.json').write_bytes(lock_bytes)
        tools = root / 'tools'
        tools.mkdir()
        exe = tools / 'yt-dlp.exe'
        receipt = tools / 'installed-files.json'
        provider = scripts / 'Video-Extractor.ps1'

        def seed():
            exe.write_bytes(old)
            receipt.write_text(json.dumps({'schema': 2, 'root': str(tools),
                'lockSha256': sha(lock_bytes), 'extractor': {'version': '2026.08.19',
                    'sha256': sha(old), 'checkedAt': '2026-01-01T00:00:00Z'}}))

        def release(digest):
            provider.write_text("function Get-LatestVideoExtractor { [pscustomobject]@{version='2026.11.23';"
                f"url='http://127.0.0.1:{server.server_port}/yt-dlp.exe';sha256='{digest}';size={len(payload)}L" + '} }')

        def run():
            return subprocess.run([SHELL, '-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass',
                '-File', str(installer), '-Destination', str(tools), '-CacheDirectory', str(root / 'cache'),
                '-RefreshExtractorOnly'], capture_output=True, timeout=60,
                creationflags=subprocess.CREATE_NO_WINDOW)

        seed()
        before = receipt.read_bytes()
        release('0' * 64)
        result = run()
        assert result.returncode != 0 and exe.read_bytes() == old and receipt.read_bytes() == before, result.stderr
        checks += 1
        release(sha(payload))
        result = run()
        assert result.returncode == 0 and exe.read_bytes() == payload, result.stderr
        saved = json.loads(receipt.read_text(encoding='utf-8-sig'))
        assert saved['schema'] == 2 and saved['extractor']['version'] == '2026.11.23'
        checks += 2
        provider.write_text("function Get-LatestVideoExtractor { throw 'offline fixture' }")
        before = receipt.read_bytes()
        result = run()
        assert result.returncode == 0 and b'within the last day' in result.stdout and receipt.read_bytes() == before, result.stderr
        checks += 1
        saved['extractor']['checkedAt'] = '2026-01-01T00:00:00Z'
        receipt.write_text(json.dumps(saved))
        before = receipt.read_bytes()
        result = run()
        assert result.returncode == 0 and b'Keeping the verified' in result.stdout and exe.read_bytes() == payload and receipt.read_bytes() == before
        checks += 1
        exe.write_bytes(b'changed unverified file')
        result = run()
        assert result.returncode != 0 and exe.read_bytes() == b'changed unverified file'
        checks += 1
finally:
    server.shutdown()
    server.server_close()
print(f'Extractor upgrade checks passed: {checks}')
