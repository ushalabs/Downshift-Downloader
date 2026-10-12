"""Exercise the actual native queue, SQLite persistence, and multi-download engine."""
import json, pathlib, subprocess, sys, tempfile, threading, time
from contextlib import contextmanager
from PerformanceServer import ControlledServer, expected_sha256, file_sha256, local_environment

class Driver:
    live=[]
    def __init__(self, exe, db):
        self.p = subprocess.Popen([exe, str(db)], stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True, encoding="utf-8", env=local_environment())
        self.live.append(self)
    def send(self, op, **values):
        self.p.stdin.write(json.dumps(dict(op=op, **values))+"\n"); self.p.stdin.flush()
        answer=json.loads(self.p.stdout.readline()); return answer
    def call(self, op, **values):
        answer=self.send(op, **values); assert answer["ok"], answer; return answer
    def rows(self): return self.call("snapshot")["entries"]
    def close(self):
        self.p.stdin.write('{"op":"exit"}\n'); self.p.stdin.flush(); self.p.communicate(timeout=15); assert self.p.returncode==0
    def kill(self): self.p.kill(); self.p.communicate(timeout=10)

@contextmanager
def workspace():
    with tempfile.TemporaryDirectory(prefix="downshift-queue-") as tmp:
        try: yield tmp
        finally:
            for d in Driver.live:
                if d.p.poll() is None: d.kill()

def wait(driver, predicate, timeout=25):
    end=time.monotonic()+timeout
    while time.monotonic()<end:
        rows=driver.rows()
        if predicate(rows): return rows
        time.sleep(.06)
    raise AssertionError(rows)

def main():
    exe=sys.argv[1]; checks=0
    server=ControlledServer(); thread=threading.Thread(target=server.serve_forever,daemon=True); thread.start()
    server.configure(2*1024*1024, per_connection_bps=2*1024*1024, header_delay=.05)
    url=f"http://127.0.0.1:{server.server_port}/range"
    with workspace() as tmp:
        root=pathlib.Path(tmp); db=root/"queue.sqlite"; d=Driver(exe,db)
        settings=d.call("snapshot")["settings"]; settings.update(directory=str(root),simultaneous=2,perHost=2,hostConnections=4,connections=4,automatic=False,speedLimit=str(1024*1024))
        d.call("configure",settings=settings)
        ids=[]
        for i in range(3): ids.append(d.call("add",url=url,destination=str(root/f"file{i}.bin"),sha256=expected_sha256(server.size))["id"])
        checks+=1
        started=time.monotonic(); peak=0; connections=0
        def finished(rows):
            nonlocal peak,connections
            peak=max(peak,sum(r["status"]==1 for r in rows)); connections=max(connections,sum(r["active"] for r in rows))
            return all(r["status"]==3 for r in rows)
        rows=wait(d,finished)
        assert peak==2 and connections<=4, (peak,connections)
        assert time.monotonic()-started>=5.5, "Global cap not respected"
        for i in range(3): assert file_sha256(root/f"file{i}.bin")==expected_sha256(server.size)
        checks+=4
        # SQL injection-looking URL and sensitive query must be encrypted in DB/WAL.
        secret=url+"?token=QUEUE_SECRET_4729';DROP_TABLE"
        pending=d.call("add",url=secret,destination=str(root/"private.bin"),start=False,priority=1)["id"]
        assert not d.send("add",url=url,destination=str(root/"private.bin"))["ok"]
        assert not d.send("add",url="file:///C:/private",destination=str(root/"bad.bin"))["ok"]
        assert not d.send("add",url="https://name:password@example.org/file",destination=str(root/"bad.bin"))["ok"]
        assert not d.send("rename",id=ids[0],destination=str(root/"renamed.bin"))["ok"]
        d.call("rename",id=pending,destination=str(root/"private-renamed.bin"))
        other=Driver(exe,db); assert not other.send("snapshot")["ok"]; other.close()
        for file in root.glob("queue.sqlite*"):
            if file.is_file(): assert b"QUEUE_SECRET_4729" not in file.read_bytes() and secret.encode() not in file.read_bytes()
        checks+=7
        d.close(); d=Driver(exe,db)
        snap=d.call("snapshot"); assert len(snap["entries"])==4 and snap["settings"]==settings
        assert next(r for r in snap["entries"] if r["id"]==pending)["priority"]==1
        checks+=2
        # Real process death leaves a Running record; startup holds it for explicit resume.
        d.call("action",id=pending,action=0)
        wait(d,lambda rows:any(r["id"]==pending and int(r["downloaded"])>100000 for r in rows)); d.kill()
        d=Driver(exe,db); row=next(r for r in d.rows() if r["id"]==pending); assert row["status"]==2
        d.call("action",id=pending,action=0); wait(d,lambda rows:next(r for r in rows if r["id"]==pending)["status"]==3)
        assert file_sha256(root/"private-renamed.bin")==expected_sha256(server.size); checks+=2
        # Per-download cap, pause/recovery, cancellation, removal preserves files.
        slow=d.call("add",url=url,destination=str(root/"slow.bin"),speedLimit=str(256*1024))["id"]
        wait(d,lambda rows:any(r["id"]==slow and int(r["downloaded"])>100000 for r in rows))
        d.call("action",id=slow,action=1); wait(d,lambda rows:next(r for r in rows if r["id"]==slow)["status"]==2)
        assert (root/"slow.bin.part").exists(); checks+=1
        d.call("action",id=slow,action=0); wait(d,lambda rows:next(r for r in rows if r["id"]==slow)["status"]==1)
        d.call("action",id=slow,action=2); wait(d,lambda rows:next(r for r in rows if r["id"]==slow)["status"]==5)
        d.call("action",id=slow,action=4); assert (root/"slow.bin.part").exists(); checks+=2
        d.call("action",id=ids[0],action=4); assert (root/"file0.bin").exists(); checks+=1
        # Priority scheduling behind a gate, one download per host.
        settings.update(simultaneous=3,perHost=1,speedLimit="0"); d.call("configure",settings=settings)
        gate=d.call("add",url=url,destination=str(root/"gate.bin"),speedLimit=str(1024*1024))["id"]
        wait(d,lambda rows:any(r["id"]==gate and r["status"]==1 for r in rows))
        low=d.call("add",url=url,destination=str(root/"low.bin"),priority=-1)["id"]
        high=d.call("add",url=url,destination=str(root/"high.bin"),priority=1)["id"]
        rows=wait(d,lambda rows:next(r for r in rows if r["id"]==high)["status"]==3)
        assert next(r for r in rows if r["id"]==low)["status"]!=3
        wait(d,lambda rows:next(r for r in rows if r["id"]==low)["status"]==3); checks+=2
        d.close(); d=Driver(exe,db); assert len(d.rows())==6; d.close(); checks+=1
        # Authenticated encrypted record corruption is a visible startup failure.
        import sqlite3
        with sqlite3.connect(db) as sql: sql.execute("UPDATE downloads SET payload=? WHERE id=?",(b"broken",high))
        sql.close()
        d=Driver(exe,db); assert not d.send("snapshot")["ok"]; d.close(); checks+=1
    server.shutdown(); server.server_close()
    print(f"Queue checks passed: {checks} (native SQLite, real transfers and forced process death)")
if __name__=="__main__": main()
