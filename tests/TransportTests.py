"""Assert independent physical connections through a real TLS/H2 server."""
import json, pathlib, subprocess, sys, tempfile, time, queue, threading
from PerformanceServer import expected_sha256, file_sha256, local_environment

def run(cli, node, output=None, require_independent=True):
    fixtures = pathlib.Path(__file__).parent / 'fixtures'
    report = []
    with tempfile.TemporaryDirectory(prefix='downshift-transport-') as tmp:
        for connections, background in ((1, 0), (8, 0), (1, 4), (8, 4)):
            size = 4 * 1024 * 1024
            server = subprocess.Popen([node, str(pathlib.Path(__file__).with_name('TransportFixture.cjs')),
                str(fixtures), str(size), str(512 * 1024 if not background else 2*1024*1024),
                str(2*1024*1024 if background else 0), str(background)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                stderr=subprocess.PIPE, text=True, env=local_environment())
            try:
                ready = queue.Queue()
                threading.Thread(target=lambda: ready.put(server.stdout.readline()), daemon=True).start()
                port = json.loads(ready.get(timeout=10))['port']
                target = pathlib.Path(tmp) / f'{connections}-{background}.bin'
                started = time.perf_counter()
                result = subprocess.run([cli, f'https://localhost:{port}/file', str(target),
                    '--connections', str(connections), '--chunk-kib', '256', '--ca-bundle', str(fixtures/'test-ca.pem'),
                    '--sha256', expected_sha256(size), '--json', '--progress-json'],
                    capture_output=True, text=True, timeout=35, env=local_environment())
                assert result.returncode == 0, result.stderr
                assert file_sha256(target) == expected_sha256(size) and target.stat().st_size == size
                metrics = json.loads(result.stdout)
                server.stdin.write('stats\n'); server.stdin.flush(); stats = json.loads(server.stdout.readline())
                row = {'slots': connections, 'backgroundSockets': background, 'wallSeconds': time.perf_counter()-started, 'metrics': metrics, **stats}
                report.append(row)
                foreground = [r for r in stats['records'] if not r['background']]
                ranges = sorted((r['first'], r['last']) for r in foreground if r['last'] > 0)
                assert ranges[0][0] == 0 and ranges[-1][1] == size-1
                assert all(a[1]+1 == b[0] for a, b in zip(ranges, ranges[1:])), 'Range gap/overlap'
                assert all(r['protocol'] == '2.0' for r in foreground), 'HTTP/2 not actually exercised'
                assert len({r['socket'] for r in foreground}) <= connections, 'Keep-alive pool churned sockets'
                assert len({r['socket'] for r in stats['records'] if r['background']}) == background
                assert stats['peakSockets'] <= connections
                if require_independent:
                    assert stats['peakSockets'] == connections, f'Ranges share sockets: {stats}'
                print(f"TLS transport: {connections} slots, {background} background sockets, {stats['peakSockets']} physical sockets, "
                      f"{stats['peakStreams']} requests, {metrics['average_bps']/1048576:.2f} MiB/s", flush=True)
            finally:
                server.stdin.write('exit\n'); server.stdin.flush()
                _, errors = server.communicate(timeout=10)
                assert server.returncode == 0, errors
    if output: pathlib.Path(output).write_text(json.dumps(report, indent=2)+'\n')
    if require_independent:
        # The fixture models per-socket caps and a fair shared link; this is not
        # an assertion about arbitrary routers, servers, or actual ISP capacity.
        assert report[1]['metrics']['average_bps'] > report[0]['metrics']['average_bps'] * 3
        assert report[3]['metrics']['average_bps'] > report[2]['metrics']['average_bps'] * 2
    return report

if __name__ == '__main__':
    run(*sys.argv[1:3], output=sys.argv[3] if len(sys.argv)>3 else None,
        require_independent='--observe' not in sys.argv)
