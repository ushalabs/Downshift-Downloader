import pathlib,subprocess,sys,tempfile,threading,ssl
from PerformanceServer import ControlledServer,expected_sha256,local_environment
exe=pathlib.Path(sys.argv[1]).resolve();report=pathlib.Path(sys.argv[2]).resolve()
server=ControlledServer();server.configure(3*1024*1024,per_connection_bps=1024*1024)
threading.Thread(target=server.serve_forever,daemon=True).start()
fixtures=pathlib.Path(__file__).parent/'fixtures'
secure=ControlledServer();secure.configure(server.size,per_connection_bps=1024*1024)
context=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);context.load_cert_chain(fixtures/'localhost-cert.pem',fixtures/'localhost-key.pem');secure.socket=context.wrap_socket(secure.socket,server_side=True)
threading.Thread(target=secure.serve_forever,daemon=True).start()
try:
    run=subprocess.run([str(exe),f'http://127.0.0.1:{server.server_port}/range',expected_sha256(server.size),str(report),f'https://localhost:{secure.server_port}/range',str(fixtures/'test-ca.pem')],env=local_environment(),capture_output=True,text=True,encoding='utf-8',timeout=180,creationflags=subprocess.CREATE_NO_WINDOW)
    print(run.stdout,end='');print(run.stderr,end='');assert run.returncode==0,'Torrent T4 mixed acceptance failed'
finally:server.shutdown();server.server_close();secure.shutdown();secure.server_close()
