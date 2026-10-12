"""Real transfers across buffer boundaries and short failed responses; independent hashes."""
import json,pathlib,subprocess,sys,tempfile,threading
from PerformanceServer import ControlledServer,expected_sha256,file_sha256,local_environment
from RecoveryTests import Server,Handler

def main():
    cli=str(pathlib.Path(sys.argv[1]).resolve());count=0
    server=ControlledServer();thread=threading.Thread(target=server.serve_forever,daemon=True);thread.start()
    try:
        with tempfile.TemporaryDirectory(prefix='buffered-tests-',dir=pathlib.Path.cwd()) as folder:
            directory=pathlib.Path(folder)
            for size in (1,65535,65536,65537,131071,131072,131073,1048575,1048576,1048577):
                for mode in ('range','stream'):
                    server.configure(size);target=directory/f'{mode}-{size}.bin';digest=expected_sha256(size)
                    run=subprocess.run([cli,f'http://127.0.0.1:{server.server_port}/{mode}',str(target),'--connections','4','--chunk-kib','256','--sha256',digest,'--json'],capture_output=True,text=True,env=local_environment(),timeout=20)
                    assert run.returncode==0,run.stderr
                    assert target.stat().st_size==size and file_sha256(target)==digest
                    result=json.loads(run.stdout)
                    assert result['bytes']==size and result['write_bytes']==size
                    assert result['checksum_read_bytes']==size*(2 if mode=='range' else 1)
                    assert result['write_calls']>0
                    if mode=='range':assert result['write_calls']<=(size+65535)//65536+result['ranges']
                    assert result['checkpoints']>=3 and result['checkpoint_seconds']>=0
                    assert not pathlib.Path(str(target)+'.part').exists()
                    count+=1
            print(f'PASS {count} independently hashed boundary transfers',flush=True)
            # Less than one buffer arrives before failure. It must be discarded, with
            # no progress underflow, no publication into recovery, and a correct retry.
            faults=Server(('127.0.0.1',0),Handler);faults.lock=threading.Lock();faults.active=0
            worker=threading.Thread(target=faults.serve_forever,daemon=True);worker.start()
            try:
                for mode in ('range','stream'):
                    faults.configure(size=512*1024,bps=0,scope=mode,fault='drop-once',drop_bytes=32769)
                    target=directory/f'short-failed-{mode}.bin';digest=expected_sha256(faults.settings['size'])
                    run=subprocess.run([cli,f'http://127.0.0.1:{faults.server_port}/{mode}',str(target),'--connections','4','--chunk-kib','256','--sha256',digest,'--json','--progress-json'],capture_output=True,text=True,env=local_environment(),timeout=20)
                    assert run.returncode==0,run.stderr
                    result=json.loads(run.stdout);progress=[json.loads(line) for line in run.stderr.splitlines()]
                    assert result['retries']>0 and file_sha256(target)==digest and target.stat().st_size==512*1024
                    assert all(0<=p['downloaded_bytes']<=512*1024 and 0<=p['verified_bytes']<=512*1024 for p in progress)
                    assert progress[-1]['verified_bytes']==512*1024
                    count+=1;print(f'PASS discard short failed {mode} response and retry',flush=True)
            finally:
                faults.shutdown();faults.server_close();worker.join(2)
    finally:
        server.shutdown();server.server_close();thread.join(2)
    print(f'{count} buffered transfer checks passed.',flush=True)
if __name__=='__main__':main()
