"""Real local media transfers through the Qt dialog, yt-dlp and FFmpeg."""
import functools
import hashlib
import http.server
import json
import os
import pathlib
import subprocess
import sys
import tempfile
import threading

def main():
    exe=pathlib.Path(sys.argv[1]).resolve()
    tools=exe.parent/'media-tools'
    if not (tools/'yt-dlp.exe').exists():
        print('Video helpers missing: install local media tools before this test.',file=sys.stderr)
        return 77
    with tempfile.TemporaryDirectory(prefix='downshift-media-') as temporary:
        root=pathlib.Path(temporary);source=root/'source';destination=root/'downloads';source.mkdir();destination.mkdir()
        ffmpeg=str(tools/'ffmpeg.exe');probe=str(tools/'ffprobe.exe')
        subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-f','lavfi','-i','testsrc=size=320x240:rate=12','-f','lavfi','-i','sine=frequency=440:sample_rate=44100','-t','2','-c:v','mpeg4','-pix_fmt','yuv420p','-c:a','aac',str(source/'fixture.mp4')],check=True,timeout=30,capture_output=True)
        (destination/'Videos').mkdir()
        (source/'broken.mp4').write_bytes((source/'fixture.mp4').read_bytes())
        (destination/'Videos'/'broken [broken].mp4').write_bytes(b'<html>No video here</html>')
        # FFmpeg's DASH muxer resolves fragment paths relative to the manifest.
        # Use an explicit working directory rather than Windows backslashes in that name.
        subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-i','fixture.mp4','-map','0:v','-map','0:a','-c','copy','-f','dash','-remove_at_exit','0','stream.mpd'],cwd=source,check=True,timeout=30,capture_output=True)
        assert (source/'init-stream0.m4s').exists() and (source/'init-stream1.m4s').exists(),'DASH fixture initialization files missing'
        class Handler(http.server.SimpleHTTPRequestHandler):
            def log_message(self,*args):pass
            def send_error(self,code,message=None,explain=None):
                if code==404:print('Missing media fixture:',self.path)
                super().send_error(code,message,explain)
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(source)))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            environment={**os.environ,'NO_PROXY':'localhost,127.0.0.1','no_proxy':'localhost,127.0.0.1'}
            completed=subprocess.run([str(exe),str(destination),f'http://127.0.0.1:{server.server_port}',sys.argv[2]],capture_output=True,text=True,encoding='utf-8',timeout=180,env=environment)
            print(completed.stdout,end='');assert completed.returncode==0,completed.stderr
            files=[file for file in (destination/'Videos').iterdir() if file.suffix in ('.mp4','.mkv') and not file.name.startswith('broken')];assert len(files)==2,files
            assert (destination/'Videos'/'broken [broken].mp4').read_bytes()==b'<html>No video here</html>','Existing output must remain untouched'
            original=hashlib.sha256((source/'fixture.mp4').read_bytes()).hexdigest()
            assert any(hashlib.sha256(file.read_bytes()).hexdigest()==original for file in files),'Direct file changed'
            for file in files:
                metadata=json.loads(subprocess.run([probe,'-v','error','-show_streams','-show_format','-of','json',str(file)],capture_output=True,text=True,check=True,timeout=20).stdout)
                assert {'audio','video'}<={stream['codec_type'] for stream in metadata['streams']},metadata
                assert 1.8<float(metadata['format']['duration'])<2.3,metadata
            assert not list(destination.glob('*.part')),'Completed media left partials'
            print('Independent checks: direct SHA-256 and both files have audio/video and expected duration.')
        finally:server.shutdown();server.server_close()
    return 0
if __name__=='__main__':sys.exit(main())
