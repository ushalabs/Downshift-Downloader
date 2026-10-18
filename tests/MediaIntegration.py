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
import time
import struct

def main():
    sys.stdout.reconfigure(encoding='utf-8');sys.stderr.reconfigure(encoding='utf-8')
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
        (source/'background.mp4').write_bytes((source/'fixture.mp4').read_bytes())
        subprocess.run([ffmpeg,'-v','error','-i',str(source/'fixture.mp4'),'-an','-c:v','copy',str(source/'browser-video.mp4')],check=True,timeout=30,capture_output=True)
        subprocess.run([ffmpeg,'-v','error','-i',str(source/'fixture.mp4'),'-vn','-c:a','copy',str(source/'browser-audio.m4a')],check=True,timeout=30,capture_output=True)
        with (source/'browser-video.mp4').open('ab') as large_video:large_video.write(struct.pack('>I4s',12*1024*1024+8,b'free')+bytes(12*1024*1024))
        (source/'pause.mp4').write_bytes((source/'fixture.mp4').read_bytes()+struct.pack('>I4s',1024*1024+8,b'free')+bytes(1024*1024))
        subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-f','lavfi','-i','testsrc2=size=320x240:rate=24','-f','lavfi','-i','sine=frequency=440:sample_rate=44100','-t','8','-c:v','mpeg2video','-b:v','1600k','-g','24','-c:a','mp2','-f','hls','-hls_time','1','-hls_playlist_type','vod','-hls_segment_filename','pause-seg%02d.ts','pause-stream.m3u8'],cwd=source,check=True,timeout=30,capture_output=True)
        (source/'collection.html').write_text('<html><title>Video collection</title><video src="fixture.mp4"></video><video src="broken.mp4"></video></html>',encoding='utf-8')
        (destination/'Videos'/'broken [broken].mp4').write_bytes(b'<html>No video here</html>')
        # FFmpeg's DASH muxer resolves fragment paths relative to the manifest.
        # Use an explicit working directory rather than Windows backslashes in that name.
        subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-i','fixture.mp4','-filter_complex','[0:v]split=2[full][small];[small]scale=160:120[scaled]','-map','[full]','-map','[scaled]','-map','0:a','-c:v','mpeg4','-c:a','copy','-f','dash','-remove_at_exit','0','stream.mpd'],cwd=source,check=True,timeout=30,capture_output=True)
        assert (source/'init-stream0.m4s').exists() and (source/'init-stream1.m4s').exists(),'DASH fixture initialization files missing'
        subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-i','fixture.mp4','-c:v','mpeg2video','-c:a','mp2','-f','hls','-hls_playlist_type','vod','-hls_segment_filename','hls-%03d.ts','hls-default.txt'],cwd=source,check=True,timeout=30,capture_output=True)
        subprocess.run([ffmpeg,'-hide_banner','-loglevel','error','-i','fixture.mp4','-vf','scale=160:120','-c:v','mpeg2video','-c:a','mp2','-f','hls','-hls_playlist_type','vod','-hls_segment_filename','hls-small-%03d.ts','hls-small.txt'],cwd=source,check=True,timeout=30,capture_output=True)
        (source/'hls-master.txt').write_text('#EXTM3U\n#EXT-X-STREAM-INF:BANDWIDTH=100000,RESOLUTION=160x120\nhls-small.txt\n#EXT-X-STREAM-INF:BANDWIDTH=400000,RESOLUTION=320x240\nhls-default.txt\n',encoding='utf-8')
        # A long manifest expands to megabytes of extractor fragment metadata,
        # even though quality selection needs only a few format fields.
        segments=''.join(f'<SegmentURL media="chunk-{number:06d}.m4s"/>' for number in range(60000))
        (source/'large.mpd').write_text('<?xml version="1.0"?><MPD xmlns="urn:mpeg:dash:schema:mpd:2011" type="static" mediaPresentationDuration="PT120000S" minBufferTime="PT1S"><Period duration="PT120000S"><AdaptationSet mimeType="video/mp4" contentType="video"><Representation id="large" bandwidth="100000" width="320" height="240" codecs="mp4v.20.9"><SegmentList timescale="1" duration="2"><Initialization sourceURL="init-stream0.m4s"/>'+segments+'</SegmentList></Representation></AdaptationSet></Period></MPD>',encoding='utf-8')
        class Handler(http.server.SimpleHTTPRequestHandler):
            def do_GET(self):
                if self.path=='/always-denied.mp4':self.send_error(403,'Controlled browser source refusal');return
                if self.path=='/browser-denied.mp4':
                    if (source/'.reject-browser').exists():self.send_error(403,'Controlled expired browser stream');return
                    self.path='/fixture.mp4'
                if self.path=='/browser-video.mp4':
                    file=source/'browser-video.mp4';size=file.stat().st_size;start=0;end=size-1;partial=False
                    if self.headers.get('Range','').startswith('bytes='):
                        bounds=self.headers['Range'][6:].split('-',1);start=int(bounds[0]);end=min(size-1,int(bounds[1])) if bounds[1] else end;partial=True
                    with (source/'browser-ranges.jsonl').open('a') as log:log.write(json.dumps({'start':start,'end':end})+'\n')
                    if start>=size:self.send_response(416);self.send_header('Content-Range',f'bytes */{size}');self.end_headers();return
                    self.send_response(206 if partial else 200);self.send_header('Content-Type','video/mp4');self.send_header('Accept-Ranges','bytes');self.send_header('Content-Length',str(end-start+1))
                    if partial:self.send_header('Content-Range',f'bytes {start}-{end}/{size}')
                    self.end_headers()
                    try:
                        with file.open('rb') as body:body.seek(start);self.wfile.write(body.read(end-start+1))
                    except (BrokenPipeError,ConnectionResetError,OSError):pass
                    return
                if self.path=='/pause.mp4' or self.path.startswith('/pause-seg'):
                    file=source/self.path.lstrip('/');size=file.stat().st_size;start=0
                    if self.headers.get('Range','').startswith('bytes='):start=int(self.headers['Range'][6:].split('-')[0])
                    with (source/'pause-requests.jsonl').open('a') as log:log.write(json.dumps({'file':file.name,'start':start})+'\n')
                    if start>=size:self.send_response(416);self.send_header('Content-Range',f'bytes */{size}');self.end_headers();return
                    self.send_response(206 if start else 200);self.send_header('Content-Type','video/mp4' if self.path.endswith('.mp4') else 'video/mp2t');self.send_header('Accept-Ranges','bytes');self.send_header('ETag','"pause-fixture-v1"');self.send_header('Content-Length',str(size-start))
                    if start:self.send_header('Content-Range',f'bytes {start}-{size-1}/{size}')
                    self.end_headers()
                    try:
                        with file.open('rb') as body:
                            body.seek(start)
                            while data:=body.read(4096):self.wfile.write(data);self.wfile.flush();time.sleep(.035)
                    except (BrokenPipeError,ConnectionResetError,OSError):pass
                    return
                if self.path.startswith('/hls-') and (self.headers.get('Referer')!=f'http://127.0.0.1:{self.server.server_port}/embed/player' or self.headers.get('Origin')!='https://player.fixture.test' or self.headers.get('User-Agent')!='DownshiftMediaFixture/1.0'):
                    self.send_error(403,'Missing media request context');return
                if self.path=='/broken.mp4' and (source/'.fail-broken').exists():
                    self.send_error(503,'Controlled transfer failure');return
                if self.path=='/background.mp4' and (source/'.hold-background').exists():
                    deadline=time.monotonic()+20
                    while not (source/'.release-background').exists() and time.monotonic()<deadline:time.sleep(.02)
                super().do_GET()
            def log_message(self,*args):pass
            def send_error(self,code,message=None,explain=None):
                if code==404:print('Missing media fixture:',self.path)
                super().send_error(code,message,explain)
        server=http.server.ThreadingHTTPServer(('127.0.0.1',0),functools.partial(Handler,directory=str(source)))
        threading.Thread(target=server.serve_forever,daemon=True).start()
        try:
            # Any attempted native YouTube extraction is unavailable; browser
            # descriptor fixtures must complete using only the supplied tracks.
            environment={**os.environ,'NO_PROXY':'localhost,127.0.0.1','no_proxy':'localhost,127.0.0.1','HTTPS_PROXY':'http://127.0.0.1:1','https_proxy':'http://127.0.0.1:1'}
            raw=subprocess.run([str(tools/'yt-dlp.exe'),'--ignore-config','--no-plugin-dirs','--no-playlist','--skip-download','--dump-single-json',f'http://127.0.0.1:{server.server_port}/large.mpd'],capture_output=True,timeout=30,env=environment)
            assert raw.returncode==0 and len(raw.stdout)>4*1024*1024,'Long-video fixture must reproduce the old metadata limit'
            print('Long-video raw metadata bytes:',len(raw.stdout),'(exceeds the old 4 MiB guard)')
            old_info={'id':'FQmqRVK3kHg','title':'Old descriptor failure','duration':2,'extractor_key':'BrowserVideo','formats':[{'url':f'http://127.0.0.1:{server.server_port}/always-denied.mp4','format_id':'18','ext':'mp4','vcodec':'unknown','acodec':'unknown','height':240}]}
            old_failure=subprocess.run([str(tools/'yt-dlp.exe'),'--ignore-config','--no-plugin-dirs','--check-formats','--skip-download','--load-info-json','-'],input=json.dumps(old_info),capture_output=True,text=True,encoding='utf-8',timeout=30,env=environment)
            assert old_failure.returncode!=0 and "KeyError: 'extractor'" in old_failure.stderr,old_failure.stderr[-800:]
            print('Original descriptor failure independently reproduced: rejected format causes missing-extractor KeyError.')
            child=subprocess.Popen([str(exe),str(destination),f'http://127.0.0.1:{server.server_port}',sys.argv[2]],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding='utf-8',env=environment)
            stdout=[];stderr=[];read_errors=[]
            def drain(pipe,lines,live):
                try:
                    for line in pipe:
                        lines.append(line)
                        if live:print(line,end='',flush=True)
                except Exception as error:read_errors.append(error)
            readers=[threading.Thread(target=drain,args=(child.stdout,stdout,False)),threading.Thread(target=drain,args=(child.stderr,stderr,True))]
            for reader in readers:reader.start()
            # Many real helper startups, pause/restart and two independent
            # browser-source transfers run sequentially. Keep each GUI wait
            # bounded while allowing the complete acceptance group to finish.
            try:code=child.wait(timeout=420)
            except subprocess.TimeoutExpired:
                child.kill();child.wait();raise RuntimeError('Media driver timeout; last checkpoints: '+''.join(stderr[-8:]))
            finally:
                for reader in readers:reader.join(timeout=5)
            assert not read_errors,read_errors
            print(''.join(stdout),end='');assert code==0,''.join(stderr)
            browser_files=list((destination/'browser-youtube').glob('*.mkv'));assert len(browser_files)==1,browser_files
            info=json.loads(subprocess.run([probe,'-v','error','-show_streams','-show_format','-of','json',str(browser_files[0])],capture_output=True,text=True,check=True,timeout=20).stdout)
            assert {'audio','video'}<={s['codec_type'] for s in info['streams']} and 1.8<float(info['format']['duration'])<2.3
            print('Browser-resolved YouTube independently verified: separate audio/video merged with native YouTube network unavailable.')
            assert any(json.loads(line)['start']>0 for line in (source/'browser-ranges.jsonl').read_text().splitlines()),'Browser-supplied large track did not use continuation chunks'
            assert not list((destination/'browser-denied').glob('*.mp4')),'Rejected browser source must not publish a completed file'
            hls_files=list((destination/'browser-youtube-hls').glob('*'));assert len(hls_files)==1,hls_files
            info=json.loads(subprocess.run([probe,'-v','error','-show_streams','-show_format','-of','json',str(hls_files[0])],capture_output=True,text=True,check=True,timeout=20).stdout)
            assert {'audio','video'}<={s['codec_type'] for s in info['streams']} and 1.8<float(info['format']['duration'])<2.3
            print('Browser-resolved HLS independently verified with required media headers and full expected duration.')
            for height in (120,240):
                selected=list((destination/f'browser-quality-{height}').glob('*'));assert len(selected)==1,selected
                streams=json.loads(subprocess.run([probe,'-v','error','-show_streams','-show_format','-of','json',str(selected[0])],capture_output=True,text=True,check=True,timeout=20).stdout)
                assert any(s.get('codec_type')=='video' and s.get('height')==height for s in streams['streams']),streams
                assert any(s.get('codec_type')=='audio' for s in streams['streams'])
            print('Browser-resolved master playlist independently verified at both selected resolutions with audio.')
            files=[file for file in (destination/'Videos').iterdir() if file.suffix in ('.mp4','.mkv') and not file.name.startswith('broken')];assert len(files)==3,files
            assert (destination/'Videos'/'broken [broken].mp4').read_bytes()==b'<html>No video here</html>','Existing output must remain untouched'
            original=hashlib.sha256((source/'fixture.mp4').read_bytes()).hexdigest()
            assert sum(hashlib.sha256(file.read_bytes()).hexdigest()==original for file in files)==2,'Each duplicate direct download must remain intact'
            for file in files:
                metadata=json.loads(subprocess.run([probe,'-v','error','-show_streams','-show_format','-of','json',str(file)],capture_output=True,text=True,check=True,timeout=20).stdout)
                assert {'audio','video'}<={stream['codec_type'] for stream in metadata['streams']},metadata
                assert 1.8<float(metadata['format']['duration'])<2.3,metadata
            assert not list(destination.glob('*.part')),'Completed media left partials'
            quality_files=list((destination/'quality copies').glob('*.mkv'));assert len(quality_files)==2,quality_files
            heights=set()
            for file in quality_files:
                metadata=json.loads(subprocess.run([probe,'-v','error','-show_streams','-show_format','-of','json',str(file)],capture_output=True,text=True,check=True,timeout=20).stdout)
                assert any(stream['codec_type']=='audio' for stream in metadata['streams'])
                heights.update(stream['height'] for stream in metadata['streams'] if stream['codec_type']=='video')
            assert heights=={120,240},heights
            print('Quality checks: separate 120p and 240p files, each with audio, independently probed.')
            print('Independent checks: direct SHA-256 and both files have audio/video and expected duration.')
            hls=list((destination/'default hls').glob('*'));assert len(hls)==1,hls
            metadata=json.loads(subprocess.run([probe,'-v','error','-show_streams','-show_format','-of','json',str(hls[0])],capture_output=True,text=True,check=True,timeout=20).stdout)
            assert {'audio','video'}<={stream['codec_type'] for stream in metadata['streams']}
            assert 1.8<float(metadata['format']['duration'])<2.3
            print('Default HLS independently verified with required Referer, Origin and User-Agent.')
            background=destination/'lifecycle'/'Renamed 100% video (1).mp4'
            assert hashlib.sha256(background.read_bytes()).hexdigest()==original,'Background transfer changed bytes'
            assert (destination/'lifecycle'/'Renamed 100% video.mp4').read_bytes()==b'preserve existing file','Renaming overwrote another file'
            renamed=list((destination/'lifecycle').rglob('Edited picture.png'));assert len(renamed)==1,renamed
            assert hashlib.sha256(renamed[0].read_bytes()).hexdigest()==original,'Browser filename edit changed bytes'
            print('Background/renamed outputs independently SHA-256 verified; existing destination preserved.')
            direct=list((destination/'pause-direct').glob('*.mp4'));assert len(direct)==1,direct
            assert hashlib.sha256(direct[0].read_bytes()).digest()==hashlib.sha256((source/'pause.mp4').read_bytes()).digest(),'Resumed direct video checksum mismatch'
            requests=[json.loads(line) for line in (source/'pause-requests.jsonl').read_text().splitlines()]
            assert any(r['file']=='pause.mp4' and r['start']>0 for r in requests),'Direct continuation did not issue a byte range'
            segmented=list((destination/'pause-hls').glob('*.mp4'));assert len(segmented)==1,segmented
            info=json.loads(subprocess.run([probe,'-v','error','-show_streams','-show_format','-of','json',str(segmented[0])],capture_output=True,text=True,check=True,timeout=20).stdout)
            assert {'video','audio'}<={s['codec_type'] for s in info['streams']} and float(info['format']['duration'])>=7.8
            def frame_hashes(file):
                decoded=subprocess.run([ffmpeg,'-v','error','-i',str(file),'-map','0:v:0','-an','-f','framemd5','-'],capture_output=True,text=True,check=True,timeout=30).stdout
                return [line.split(',')[-1].strip() for line in decoded.splitlines() if line and not line.startswith('#')]
            assert frame_hashes(segmented[0])==frame_hashes(source/'pause-stream.m3u8'),'Resumed HLS lost or altered decoded video frames'
            assert any(request['file'].startswith('pause-seg') for request in requests)
            print('Pause/resume independently verified: nonzero direct byte range plus exact SHA-256; resumed HLS retains full audio/video duration.')
        finally:server.shutdown();server.server_close()
    return 0
if __name__=='__main__':sys.exit(main())
