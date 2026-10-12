"""Isolated Chrome end-to-end fixture: public, cookie-required, and unsupported files."""
from PerformanceServer import ControlledServer, Handler
import urllib.parse
class ChromeHandler(Handler):
    def log_message(self,format,*values):
        import sys
        print("Chrome fixture: "+format%values,file=sys.stderr,flush=True)
    def do_GET(self):
        import sys
        print("Chrome fixture request range="+str(self.headers.get("Range")),file=sys.stderr,flush=True)
        if self.path.startswith("/page"):
            html=b'''<!doctype html><html><title>Downshift browser fixture</title><body><h1>Local download checks</h1><a id="public" href="/range?kind=public" download>Public file</a><a id="auth" href="/range?kind=auth" download>Authenticated file</a><a id="stream" href="/stream?kind=stream" download>Streaming file</a><a id="restart" href="/range?kind=restart" download>Worker restart file</a></body></html>'''
            self.send_response(200);self.send_header("Content-Type","text/html");self.send_header("Content-Length",str(len(html)));self.end_headers();self.wfile.write(html);return
        query=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query)
        if query.get("kind")==["auth"] and "fixture_session=authorized" not in self.headers.get("Cookie",""):
            self.send_error(401);return
        # Chrome resumes with open-ended HTTP ranges, which the engine's narrow fixture
        # did not need. Serve those according to the same exact byte representation.
        raw=self.headers.get("Range","")
        if raw.startswith("bytes=") and raw.endswith("-"):
            self.headers.replace_header("Range",raw+str(self.server.size-1))
        super().do_GET()
    def end_headers(self):
        if self.path.startswith(("/range","/stream")) and getattr(self,"_headers_buffer",[]) and b" 401 " not in self._headers_buffer[0]:
            kind=urllib.parse.parse_qs(urllib.parse.urlsplit(self.path).query).get("kind",["file"])[0]
            self.send_header("Content-Type","application/octet-stream");self.send_header("Content-Disposition",f'attachment; filename="{kind}.bin"')
        super().end_headers()
class ChromeServer(ControlledServer):
    def __init__(self):
        super().__init__();self.RequestHandlerClass=ChromeHandler
if __name__=="__main__":
    server=ChromeServer();server.configure(4*1024*1024,per_connection_bps=1024*1024)
    print(server.server_port,flush=True);server.serve_forever()
