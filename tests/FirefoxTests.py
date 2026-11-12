"""Real Firefox acceptance; pass DownshiftTestNativeHost.exe for the private pipe."""
import hashlib
import json
import pathlib
import queue
import shutil
import socket
import subprocess
import sys
import threading
import time
import urllib.error
import urllib.request
import uuid
import winreg

ROOT = pathlib.Path(__file__).resolve().parent.parent
ADDON = 'downshift@ushalabs'
HOST = 'org.ushalabs.downshift.test'
REGISTRY = 'Software\\Mozilla\\NativeMessagingHosts\\' + HOST


def until(condition, seconds=45):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = condition()
        if result:
            return result
        time.sleep(.1)
    raise AssertionError('Firefox acceptance condition timed out')


def digest(path):
    return hashlib.sha256(pathlib.Path(path).read_bytes()).hexdigest()


def start_child(arguments):
    child = subprocess.Popen(arguments, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                             stderr=subprocess.DEVNULL, text=True, encoding='utf-8')
    lines = queue.Queue()
    threading.Thread(target=lambda: [lines.put(line.rstrip()) for line in child.stdout], daemon=True).start()
    return child, lines


def main():
    firefox, gecko, driver, native_host, extension_source, output = map(pathlib.Path, sys.argv[1:])
    if native_host.name.lower() != 'downshifttestnativehost.exe':
        raise ValueError('Firefox acceptance requires the isolated DownshiftTestNativeHost.exe.')
    output.mkdir(parents=True, exist_ok=True)
    fixture = output / ('run-' + uuid.uuid4().hex)
    fixture.mkdir()
    extension = fixture / 'extension'
    shutil.copytree(extension_source, extension)
    background = extension / 'background.js'
    background.write_text(background.read_text(encoding='utf-8').replace('org.ushalabs.downshift', HOST), encoding='utf-8')
    (extension / 'test-control.html').write_text('<!doctype html><textarea id="request"></textarea><button id="send">Send</button><pre id="response"></pre><script src="test-control.js"></script>', encoding='utf-8')
    (extension / 'test-control.js').write_text('''document.querySelector('#send').addEventListener('click',async()=>{
      try{const input=JSON.parse(document.querySelector('#request').value);
        const result=input.type==='items' ? await browser.downloads.search({}):await browser.runtime.sendMessage(input);
        document.querySelector('#response').textContent=JSON.stringify({ok:true,result});
      }catch(error){document.querySelector('#response').textContent=JSON.stringify({ok:false,error:String(error)});}
    });''', encoding='utf-8')
    native = fixture / 'native'
    native.mkdir()
    downloads = fixture / 'browser-files'
    downloads.mkdir()
    previous = None
    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, REGISTRY, 0, winreg.KEY_READ | winreg.KEY_WOW64_64KEY) as key:
            previous = winreg.QueryValueEx(key, '')
    except FileNotFoundError:
        pass
    manifest = fixture / (HOST + '.json')
    manifest.write_text(json.dumps(dict(name=HOST, description='Downshift isolated Firefox test',
        path=str(native_host.resolve()), type='stdio', allowed_extensions=[ADDON])), encoding='utf-8')
    with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, REGISTRY, 0, winreg.KEY_WRITE | winreg.KEY_WOW64_64KEY) as key:
        winreg.SetValueEx(key, '', 0, winreg.REG_SZ, str(manifest.resolve()))
    with socket.socket() as reservation:
        reservation.bind(('127.0.0.1', 0))
        port = reservation.getsockname()[1]
    gecko_log = open(fixture / 'geckodriver.log', 'w', encoding='utf-8')
    gecko_process = subprocess.Popen([str(gecko), '--port', str(port), '--allow-system-access'], stdout=gecko_log, stderr=subprocess.STDOUT)
    session = None
    service = server = None
    checks = 0

    def web(method, suffix, value=None):
        request = urllib.request.Request('http://127.0.0.1:' + str(port) + suffix,
            data=json.dumps(value).encode() if value is not None else None, method=method,
            headers={'Content-Type': 'application/json'})
        try:
            with urllib.request.urlopen(request, timeout=70) as response:
                answer = json.load(response)['value']
        except urllib.error.HTTPError as error:
            raise AssertionError(error.read().decode()) from error
        return answer

    def command(method, suffix, value=None):
        return web(method, '/session/' + session + suffix, value)

    def script(source, *arguments):
        return command('POST', '/execute/sync', dict(script=source, args=list(arguments)))

    def navigate(url):
        command('POST', '/url', dict(url=url))

    def click(selector):
        element = command('POST', '/element', dict(using='css selector', value=selector))
        command('POST', '/element/' + element['element-6066-11e4-a52e-4f735466cecf'] + '/click', {})

    def native_command(text):
        service.stdin.write(text + '\n')
        service.stdin.flush()
        answer=service_lines.get(timeout=30)
        return None if answer=='confirmed' else json.loads(answer)

    def rows():
        native_command('confirm')
        return native_command('snapshot')

    def control(value):
        script("document.querySelector('#response').textContent='';document.querySelector('#request').value=arguments[0];document.querySelector('#send').click()", json.dumps(value))
        answer = json.loads(until(lambda: script("return document.querySelector('#response').textContent")))
        assert answer['ok'], answer
        return answer['result']

    def tab(handle):
        command('POST', '/window', dict(handle=handle))

    try:
        def gecko_ready():
            try:
                return web('GET', '/status')['ready']
            except (OSError, urllib.error.URLError):
                return False
        until(gecko_ready)
        addon_uuid = str(uuid.uuid4())
        started = web('POST', '/session', {'capabilities': {'alwaysMatch': {
            'browserName': 'firefox', 'moz:firefoxOptions': {'binary': str(firefox.resolve()), 'args': ['-headless'], 'prefs': {
                'extensions.webextensions.uuids': json.dumps({ADDON: addon_uuid}),
                'browser.download.folderList': 2, 'browser.download.dir': str(downloads.resolve()),
                'browser.download.useDownloadDir': True, 'browser.download.always_ask_before_handling_new_types': False,
                'browser.helperApps.neverAsk.saveToDisk': 'application/octet-stream,application/zip,image/png,video/mp4',
                'browser.shell.checkDefaultBrowser': False, 'browser.startup.page': 0
            }}}}})
        session = started['sessionId']
        installed = command('POST', '/moz/addon/install', dict(path=str(extension.resolve()), temporary=True))
        assert installed == ADDON, installed
        checks += 1
        navigate('moz-extension://' + addon_uuid + '/test-control.html')
        popup_tab = command('GET', '/window')
        service, service_lines = start_child([str(driver), str(native), '--isolated-browser-test'])
        assert service_lines.get(timeout=30) == 'ready'
        until(lambda: control({'type': 'status'}).get('connected'))
        assert control({'type': 'status'})['captureProtocol'] == 4
        checks += 1

        tools = native_host.parent / 'media-tools'
        video, segment = fixture / 'fixture.mp4', fixture / 'fixture.ts'
        subprocess.run([str(tools / 'ffmpeg.exe'), '-hide_banner', '-loglevel', 'error', '-f', 'lavfi', '-i', 'testsrc=size=320x240:rate=12', '-f', 'lavfi', '-i', 'sine=frequency=440', '-t', '2', '-c:v', 'mpeg4', '-pix_fmt', 'yuv420p', '-c:a', 'aac', str(video)], check=True, capture_output=True)
        subprocess.run([str(tools / 'ffmpeg.exe'), '-hide_banner', '-loglevel', 'error', '-i', str(video), '-c:v', 'mpeg2video', '-c:a', 'mp2', '-f', 'mpegts', str(segment)], check=True, capture_output=True)
        server, server_lines = start_child([sys.executable, str(ROOT / 'tests/ChromeFixture.py'), str(video), str(segment)])
        base = 'http://127.0.0.1:' + server_lines.get(timeout=30)
        page_tab = command('POST', '/window/new', {'type': 'tab'})['handle']
        tab(page_tab)
        navigate(base + '/page')
        command('POST', '/cookie', {'cookie': {'name': 'fixture_session', 'value': 'authorized', 'path': '/', 'httpOnly': True}})
        expected = hashlib.sha256(bytes(range(256)) * (4 * 1024 * 1024 // 256)).hexdigest()
        for kind in ('public', 'auth', 'tiny', 'unusual', 'redirect'):
            print('Firefox download case:',kind,flush=True)
            tab(page_tab)
            click('#' + kind)
            def completed():
                found = [row for row in rows() if not row['media'] and ('kind=' + kind) in row['url'] and row['status'] == 3]
                return found[-1] if found else None
            row = until(completed, 90)
            wanted = hashlib.sha256(b'Downshift tiny attachment\n').hexdigest() if kind == 'tiny' else expected
            assert digest(row['destination']) == wanted, row
            checks += 1

        for kind, source in (('blob', "URL.createObjectURL(new Blob([new Uint8Array([1,2,3,4])]))"), ('data', "'data:application/octet-stream;base64,AQIDBA=='")):
            tab(page_tab)
            script("const a=document.createElement('a');a.id=arguments[0];a.download=arguments[0]+'.bin';a.href=" + source + ";a.textContent='Download';document.body.append(a)", kind)
            before = len(rows())
            click('#' + kind)
            def generated():
                found = [row for row in rows() if not row['media'] and row['status'] == 3]
                return found[-1] if len(rows()) > before and any(pathlib.Path(row['destination']).name == kind + '.bin' for row in found) else None
            until(generated, 90)
            matches = [row for row in rows() if pathlib.Path(row['destination']).name == kind + '.bin']
            assert pathlib.Path(matches[0]['destination']).read_bytes() == bytes([1, 2, 3, 4])
            checks += 1

        tab(page_tab)
        script("const f=document.createElement('form');f.method='POST';f.action=arguments[0]+'/post?kind=post';const b=document.createElement('button');b.id='post';b.type='submit';b.textContent='Download POST';f.append(b);document.body.append(f)", base)
        click('#post')
        posted=until(lambda: next((row for row in rows() if not row['media'] and 'kind=post' in row['url'] and row['status']==3),None),90)
        assert pathlib.Path(posted['destination']).read_bytes()==b'original authorized POST response\0'*8192
        checks += 1

        tab(page_tab)
        script("const v=document.createElement('video');v.src=arguments[0]+'/video.mp4';v.controls=true;v.style='display:block;width:480px;height:270px;margin-top:35px';document.body.append(v)", base)
        until(lambda: script("return !!document.querySelector('[data-downshift-video]')"))
        click('[data-downshift-video]')
        until(lambda: any(item.get('ready') for item in native_command('media')))
        native_command('media-download')
        media_row = until(lambda: next((row for row in rows() if row['media'] and row['status'] == 3), None), 90)
        assert digest(media_row['destination']) == digest(video)
        checks += 1
        tab(page_tab)
        navigate(base+'/media-root')
        frame=command('POST','/element',dict(using='css selector',value='iframe'))
        command('POST','/frame',{'id':frame})
        until(lambda: script("return !!document.querySelector('[data-downshift-video]')"))
        click('[data-downshift-video]')
        command('POST','/frame',{'id':None})
        until(lambda: any(item.get('ready') for item in native_command('media')))
        native_command('media-download')
        embedded=until(lambda: next((row for row in rows() if row['media'] and row['url'].endswith('/manifest.txt') and row['status']==3),None),90)
        probe=subprocess.run([str(tools/'ffprobe.exe'),'-v','error','-show_streams','-of','json',embedded['destination']],check=True,capture_output=True,text=True)
        assert {stream['codec_type'] for stream in json.loads(probe.stdout)['streams']}=={'audio','video'}
        assert embedded['videoMetadata']['requestContext']['referer']==base.replace('127.0.0.1','localhost')+'/media-embed'
        checks += 1
        accepted=rows()
        assert len(accepted)==10 and all(row['status']==3 for row in accepted),'Each browser download creates one completed native task.'
        checks += 1
        tab(popup_tab)
        until(lambda: control({'type': 'items'}) == [])
        checks += 1
        (output / 'report.json').write_text(json.dumps(dict(checks=checks, firefoxVersion=started['capabilities']['browserVersion'], addonId=ADDON,
            nativeHostSha256=digest(native_host), privateProfile=True, normalRegistryRestored=True,
            directHttpDownloads=True, authenticatedGet=True, postResponse=True, tinyAttachment=True, redirect=True, blobAndData=True,
            videoOverlayAndNativeCompletion=True, embeddedHlsWithRequiredHeaders=True, noDuplicateTasks=True, browserTransportsRemoved=True), indent=2), encoding='utf-8')
        print('Actual Firefox checks passed:', checks)
    except Exception:
        failure={}
        if(service and service.poll() is None):
            try:
                failure['native']=native_command('snapshot')
            except Exception as error:
                failure['nativeError']=str(error)
        if session:
            try:
                tab(popup_tab)
                failure['downloads']=control({'type':'items'})
                failure['status']=control({'type':'status'})
            except Exception as error:
                failure['browserError']=str(error)
        (output/'failure.json').write_text(json.dumps(failure,indent=2),encoding='utf-8')
        raise
    finally:
        if session:
            try:
                command('DELETE', '')
            except Exception:
                pass
        for child in (service, server, gecko_process):
            if child and child.poll() is None:
                child.terminate()
                try:
                    child.wait(timeout=15)
                except subprocess.TimeoutExpired:
                    child.kill()
        gecko_log.close()
        if previous is None:
            try:
                winreg.DeleteKeyEx(winreg.HKEY_CURRENT_USER, REGISTRY, winreg.KEY_WOW64_64KEY)
            except FileNotFoundError:
                pass
        else:
            with winreg.CreateKeyEx(winreg.HKEY_CURRENT_USER, REGISTRY, 0, winreg.KEY_WRITE | winreg.KEY_WOW64_64KEY) as key:
                winreg.SetValueEx(key, '', 0, previous[1], previous[0])


if __name__ == '__main__':
    main()
