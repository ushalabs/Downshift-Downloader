import subprocess, sys, tempfile, threading
from PerformanceServer import ControlledServer, expected_sha256, file_sha256, local_environment
import pathlib
server=ControlledServer();server.configure(2*1024*1024,per_connection_bps=1024*1024)
threading.Thread(target=server.serve_forever,daemon=True).start()
with tempfile.TemporaryDirectory(prefix="downshift-gui-") as tmp:
    result=subprocess.run([sys.argv[1],tmp,f"http://127.0.0.1:{server.server_port}/range",expected_sha256(server.size),sys.argv[2]],capture_output=True,text=True,env=local_environment(),timeout=60)
    print(result.stdout);print(result.stderr)
    assert result.returncode==0,"Native GUI scenario failed"
    assert file_sha256(pathlib.Path(tmp)/"proof.bin")==expected_sha256(server.size),"Independent GUI download checksum"
server.shutdown();server.server_close()
