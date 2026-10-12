"""Probe installed IDM's documented CLI with an isolated, independently verified local file."""
import argparse,ctypes,datetime,json,os,pathlib,subprocess,sys,tempfile,threading,time
from ctypes import wintypes
ROOT=pathlib.Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tests'))
from PerformanceServer import ControlledServer,Handler,expected_sha256,file_sha256

class Compatible(Handler):
    def do_GET(self):
        self.server.requests.append({'method':'GET','range':self.headers.get('Range')})
        value=self.headers.get('Range','')
        if value.startswith('bytes=') and value.endswith('-'):
            self.headers.replace_header('Range',value+str(self.server.size-1))
        super().do_GET()
    def do_HEAD(self):
        self.server.requests.append({'method':'HEAD','range':self.headers.get('Range')})
        self.send_response(200);self.send_header('Content-Length',str(self.server.size));self.send_header('ETag',f'"pattern-{self.server.size}"');self.end_headers()
    def end_headers(self):
        self.send_header('Accept-Ranges','bytes');self.send_header('Content-Type','application/octet-stream');super().end_headers()

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe',type=pathlib.Path,default=pathlib.Path(os.environ.get('ProgramFiles(x86)',r'C:\Program Files (x86)'))/'Internet Download Manager/IDMan.exe')
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args();args.exe=args.exe.resolve();args.output=args.output.resolve()
    if args.output.exists():parser.error('Choose a fresh report path')
    if not args.exe.is_file():parser.error('IDM executable is absent')
    kernel=ctypes.WinDLL('kernel32',use_last_error=True)
    kernel.CreateFileW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID,wintypes.DWORD,wintypes.DWORD,wintypes.HANDLE]
    kernel.CreateFileW.restype=wintypes.HANDLE;kernel.CloseHandle.argtypes=[wintypes.HANDLE]
    powershell=pathlib.Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
    quoted=str(args.exe).replace("'","''")
    version=subprocess.check_output([str(powershell),'-NoProfile','-Command',f"(Get-Item -LiteralPath '{quoted}').VersionInfo.FileVersion"],text=True).strip()
    server=ControlledServer();server.RequestHandlerClass=Compatible;server.configure(4*1048576)
    server.requests=[]
    worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    parent=ROOT/'build/idm-probe';parent.mkdir(parents=True,exist_ok=True)
    report={'started_utc':datetime.datetime.now(datetime.timezone.utc).isoformat(),'version':version,'executable_sha256':file_sha256(args.exe),'available_for_benchmark':False,'attempts':[],
        'scope':'CLI availability only; no accepted IDM throughput/CPU comparison','documentation':'https://www.internetdownloadmanager.com/support/command_line.html'}
    try:
        with tempfile.TemporaryDirectory(prefix='run-',dir=parent) as folder:
            target=pathlib.Path(folder)/'probe.bin';expected=expected_sha256(server.size)
            for syntax in ('documented-parameter-order','documented-example-order'):
                if syntax=='documented-parameter-order':parameters=['/d',f'http://127.0.0.1:{server.server_port}/range','/p',folder,'/f',target.name,'/n']
                else:parameters=['/n','/d',f'http://127.0.0.1:{server.server_port}/range','/p',folder,'/f',target.name]
                startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
                began=time.perf_counter();process=subprocess.Popen([str(args.exe),*parameters],startupinfo=startup)
                complete=False
                while time.perf_counter()-began<30:
                    if target.exists() and target.stat().st_size==server.size:
                        handle=kernel.CreateFileW(str(target),0x80000000,0,None,3,0,None)
                        if handle!=ctypes.c_void_p(-1).value:
                            kernel.CloseHandle(handle)
                            if file_sha256(target)==expected:complete=True;break
                    if process.poll() not in (None,0) and not server.records and time.perf_counter()-began>.5:break
                    time.sleep(.1)
                attempt={'syntax':syntax,'return_code':process.poll(),'seconds':time.perf_counter()-began,'server_requests':list(server.requests),'download_ranges':list(server.records),'file_exists':target.exists(),'sha256_verified':complete,'launched_pid':process.pid}
                report['attempts'].append(attempt)
                if not complete and process.poll() is None:
                    # This Popen PID belongs to this probe, never a pre-existing instance.
                    process.terminate();process.wait(10);attempt['closed_own_failed_launcher']=True
                if complete:report['available_for_benchmark']=True;break
                if target.exists():break # Preserve an incomplete attempt; do not enqueue another file.
                if server.active:break
    finally:
        server.shutdown();server.server_close();worker.join(2)
        # Never close an existing IDM instance, start its scheduler, or change settings.
        args.output.parent.mkdir(parents=True,exist_ok=True);args.output.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))
if __name__=='__main__':main()
