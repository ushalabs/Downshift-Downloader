"""Isolated Chrome end-to-end fixture: public, cookie-required, and unsupported files."""
from PerformanceServer import ControlledServer, Handler
import urllib.parse
class ChromeHandler(Handler):
    def do_POST(self):
        self.rfile.read(int(self.headers.get('Content-Length','0')))
        payload=b'original authorized POST response\0'*8192
        self.send_response(200);self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Disposition','attachment; filename="posted.rbxmx"');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload)
    def log_message(self,format,*values):
        import sys
        print("Chrome fixture: "+format%values,file=sys.stderr,flush=True)
    def do_GET(self):
        import sys
        print("Chrome fixture request range="+str(self.headers.get("Range")),file=sys.stderr,flush=True)
        if self.path.startswith('/media-root'):
            payload=f'<!doctype html><title>Embedded media fixture</title><iframe style="width:650px;height:400px" src="http://localhost:{self.server.server_port}/media-embed"></iframe>'.encode()
            self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload);return
        if self.path.startswith('/media-embed'):
            payload=b'''<!doctype html><title>Unknown embedded player</title><video style="width:480px;height:270px" controls></video><script>document.querySelector('video').src=URL.createObjectURL(new Blob([],{type:'video/mp4'}));fetch('/manifest.txt').then(r=>r.text()).then(()=>fetch('/variant.txt')).then(r=>r.text());</script>'''
            self.send_response(200);self.send_header('Content-Type','text/html');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload);return
        if self.path in ('/manifest.txt','/variant.txt','/media-segment'):
            expected=f'http://localhost:{self.server.server_port}/media-embed'
            if self.headers.get('Referer')!=expected:self.send_error(403,'Missing embedded player referrer');return
            if self.path=='/manifest.txt':payload=b'#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=500000\n/variant.txt\n'
            elif self.path=='/variant.txt':payload=b'#EXTM3U\n#EXT-X-VERSION:3\n#EXT-X-TARGETDURATION:3\n#EXT-X-MEDIA-SEQUENCE:0\n#EXT-X-PLAYLIST-TYPE:VOD\n#EXTINF:2.0,\n/media-segment\n#EXT-X-ENDLIST\n'
            else:payload=self.server.segment_file.read_bytes()
            self.send_response(200);self.send_header('Content-Type','video/mp2t' if self.path=='/media-segment' else 'text/plain');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload);return
        if self.path.startswith('/picture'):
            payload=self.server.picture_bytes;self.send_response(200);self.send_header('Content-Type','image/png');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload);return
        if self.path.startswith("/video.mp4") and getattr(self.server,'media_file',None):
            payload=self.server.media_file.read_bytes();self.send_response(200);self.send_header('Content-Type','video/mp4');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload);return
        if self.path.startswith("/page"):
            html=b'''<!doctype html><html><title>Downshift browser fixture</title><body><h1>Local download checks</h1><a id="public" href="/range?kind=public">Public file</a><a id="auth" href="/range?kind=auth">Authenticated file</a><a id="stream" href="/stream?kind=stream">Streaming file</a><a id="restart" href="/range?kind=restart">Worker restart file</a><a id="direct" href="/range?kind=direct" download="direct.bin">Direct file</a><a id="tiny" href="/tiny?kind=tiny">Tiny attachment</a><a id="unusual" href="/asset.rbxmx?kind=unusual">Unlisted extension</a><a id="redirect" href="/redirect?kind=redirect">Redirect attachment</a></body></html>'''
            self.send_response(200);self.send_header("Content-Type","text/html");self.send_header("Content-Length",str(len(html)));self.end_headers();self.wfile.write(html);return
        if self.path.startswith("/tiny"):
            payload=b'Downshift tiny attachment\n';self.send_response(200);self.send_header('Content-Type','application/octet-stream');self.send_header('Content-Disposition','attachment; filename="tiny.rbxmx"');self.send_header('Content-Length',str(len(payload)));self.end_headers();self.wfile.write(payload);return
        if self.path.startswith("/redirect"):
            self.send_response(302);self.send_header('Location','/range?kind=redirect');self.send_header('Content-Length','0');self.end_headers();return
        query=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        if query.get("kind",[''])[0] in ('auth','pause') and "fixture_session=authorized" not in self.headers.get("Cookie",""):
            self.send_error(401);return
        # Chrome resumes with open-ended HTTP ranges, which the engine's narrow fixture
        # did not need. Serve those according to the same exact byte representation.
        raw=self.headers.get("Range","")
        if raw.startswith("bytes=") and raw.endswith("-"):
            self.headers.replace_header("Range",raw+str(self.server.size-1))
        super().do_GET()
    def end_headers(self):
        if self.path.startswith(("/range","/stream","/asset.rbxmx")) and getattr(self,"_headers_buffer",[]) and b" 401 " not in self._headers_buffer[0]:
            kind=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query).get("kind",["file"])[0]
            self.send_header("Content-Type","application/octet-stream");self.send_header("Content-Disposition",f'attachment; filename="{kind}.bin"')
        super().end_headers()
class ChromeServer(ControlledServer):
    def __init__(self):
        super().__init__();self.RequestHandlerClass=ChromeHandler
        import struct,zlib
        def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
        pixels=b''.join(b'\0'+bytes(value for x in range(32) for value in (x*7,y*11,123)) for y in range(16))
        self.picture_bytes=b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>IIBBBBB',32,16,8,2,0,0,0))+chunk(b'IDAT',zlib.compress(pixels))+chunk(b'IEND',b'')
if __name__=="__main__":
    import pathlib,sys
    server=ChromeServer();server.configure(4*1024*1024,per_connection_bps=1024*1024)
    if len(sys.argv)>1:server.media_file=pathlib.Path(sys.argv[1])
    if len(sys.argv)>2:server.segment_file=pathlib.Path(sys.argv[2])
    print(server.server_port,flush=True);server.serve_forever()
