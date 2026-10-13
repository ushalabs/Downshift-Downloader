"""Native Messaging framing, actual named pipes, receipts, recovery and actual engine files."""
import json, pathlib, struct, subprocess, sys, tempfile, threading, time, uuid
from PerformanceServer import ControlledServer, expected_sha256, file_sha256, local_environment
HOST_ID="empnpdkfnffpcgnfgdndkbielplcoioe"
def host(exe,request,origin=None,raw=None):
    data=json.dumps(request).encode() if raw is None else raw
    result=subprocess.run([exe,origin or f"chrome-extension://{HOST_ID}/"],input=struct.pack("<I",len(data))+data,capture_output=True,timeout=20,env=local_environment())
    if not result.stdout:return result.returncode,None
    size=struct.unpack("<I",result.stdout[:4])[0];assert size==len(result.stdout)-4 and size<=65536
    return result.returncode,json.loads(result.stdout[4:])
def request(exe,command,**values):
    rc,result=host(exe,dict(version=1,command=command,**values));assert rc==0 and result["ok"],result;return result
def start(exe,root):
    p=subprocess.Popen([exe,str(root)],stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True,encoding="utf-8",env=local_environment());assert p.stdout.readline().strip()=="ready";return p
def rows(p):p.stdin.write("snapshot\n");p.stdin.flush();return json.loads(p.stdout.readline())
def close(p):p.stdin.write("exit\n");p.stdin.flush();p.communicate(timeout=15);assert p.returncode==0
def main():
    exe,driver=sys.argv[1:3];checks=0;server=ControlledServer();server.configure(2*1024*1024,per_connection_bps=1024*1024);threading.Thread(target=server.serve_forever,daemon=True).start();url=f"http://127.0.0.1:{server.server_port}/range"
    assert host(exe,{"version":1,"command":"ping"},"chrome-extension://aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa/")[0]==2;checks+=1
    for data in (b"[]",b"{broken",b'{"version":1,"command":"erase"}',b'{"version":1,"command":"ping","extra":1}',b'{"version":2,"command":"ping"}',b'{"version":1,"command":"media","url":"file:///C:/test.mp4","title":"Video"}',b'{"version":1,"command":"media","url":"https://name:secret@example.com/v.mp4","title":"Video"}',b'{"version":1,"command":"media","url":"https://example.com/v.mp4","title":"Video","arguments":"--exec evil"}'):
        assert not host(exe,{},raw=data)[1]["ok"];checks+=1
    p=subprocess.run([exe,f"chrome-extension://{HOST_ID}/"],input=struct.pack("<I",65537),capture_output=True,timeout=5);assert p.returncode==3;checks+=1
    assert not host(exe,{"version":1,"command":"ping"})[1]["ok"];checks+=1
    with tempfile.TemporaryDirectory(prefix="downshift-browser-") as tmp:
        root=pathlib.Path(tmp);p=start(driver,root)
        try:
            assert request(exe,"ping")["state"]=="ready";checks+=1
            assert not host(exe,dict(version=1,command="media",url="https://www.youtube.com/watch?v=undefined",title="Video"))[1]["ok"];checks+=1
            key=uuid.uuid4().hex;values=dict(key=key,url=url,filename="browser.bin",total=str(server.size))
            accepted=request(exe,"prepare",**values);assert accepted["state"]=="prepared";assert rows(p)[0]["status"]==6 and not (root/"Programs"/"browser.bin.part").exists();checks+=1
            assert request(exe,"prepare",**values)["downloadId"]==accepted["downloadId"] and len(rows(p))==1;checks+=1
            assert not host(exe,dict(version=1,command="prepare",**{**values,"url":url+"?other"}))[1]["ok"];checks+=1
            assert not host(exe,dict(version=1,command="commit",key=key,browserCancelled=False))[1]["ok"];checks+=1
            assert not host(exe,dict(version=1,command="commit",key=key,browserCancelled=True,browserNotStarted=True))[1]["ok"];checks+=1
            close(p);p=start(driver,root);assert request(exe,"status",key=key)["state"]=="prepared";checks+=1
            request(exe,"commit",key=key,browserCancelled=True);request(exe,"commit",key=key,browserCancelled=True)
            end=time.monotonic()+15
            while time.monotonic()<end:
                if rows(p)[0]["status"]==3:break
                time.sleep(.05)
            assert file_sha256(root/"Programs"/"browser.bin")==expected_sha256(server.size) and len(rows(p))==1;checks+=1
            assert request(exe,"abort",key=key)["state"]=="committed";checks+=1
            other=uuid.uuid4().hex;request(exe,"prepare",key=other,url=url,filename="aborted.bin");request(exe,"abort",key=other);assert request(exe,"commit",key=other,browserCancelled=True)["state"]=="aborted" and not (root/"aborted.bin.part").exists();checks+=1
            for filename in ("../escape.bin","C:\\escape.bin","CON","evil\\file.bin"):
                assert not host(exe,dict(version=1,command="prepare",key=uuid.uuid4().hex,url=url,filename=filename))[1]["ok"];checks+=1
            assert not host(exe,dict(version=1,command="prepare",key=uuid.uuid4().hex,url=url,filename="wrong-size.bin",total="10000000"))[1]["ok"];checks+=1
            assert request(exe,"prepare",key=uuid.uuid4().hex,url=url.replace("/range","/stream"),filename="stream.bin")["state"]=="prepared";checks+=1
            # Changed preflight validator is rejected by the real download before completion.
            changed=uuid.uuid4().hex;request(exe,"prepare",key=changed,url=url,filename="changed.bin");server.weak_etag=True;request(exe,"commit",key=changed,browserCancelled=True)
            end=time.monotonic()+10
            while time.monotonic()<end:
                row=next(r for r in rows(p) if r["handoffKey"]==changed)
                if row["status"]==4:break
                time.sleep(.05)
            assert row["status"]==4 and not (root/"Programs"/"changed.bin").exists();checks+=1
            p.stdin.write("remove "+accepted["downloadId"]+"\n");p.stdin.flush();json.loads(p.stdout.readline())
            assert request(exe,"status",key=key)["state"]=="committed" and request(exe,"prepare",**values)["state"]=="committed";checks+=1
            assert len(rows(p))==3 and (root/"Programs"/"browser.bin").exists();checks+=1
            server.weak_etag=False;direct=uuid.uuid4().hex;request(exe,"prepare",key=direct,url=url,filename="direct.bin")
            request(exe,"commit",key=direct,browserNotStarted=True);request(exe,"commit",key=direct,browserNotStarted=True)
            end=time.monotonic()+10
            while time.monotonic()<end:
                if next(r for r in rows(p) if r["handoffKey"]==direct)["status"]==3:break
                time.sleep(.05)
            assert file_sha256(root/"Programs"/"direct.bin")==expected_sha256(server.size);checks+=1
            close(p);p=None
            for file in root.glob("queue.sqlite*"):
                if file.is_file():assert url.encode() not in file.read_bytes()
            checks+=1
        finally:
            if p and p.poll() is None:p.kill();p.communicate(timeout=5)
    server.shutdown();server.server_close();print(f"Native browser checks passed: {checks}")
if __name__=="__main__":main()
