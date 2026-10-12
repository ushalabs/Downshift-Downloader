"""Paired Phase 7/8 benchmarks: alternate binaries, independently verify every file."""
from __future__ import annotations
import argparse,base64,csv,datetime,hashlib,json,os,pathlib,random,shutil,statistics,subprocess,sys,tempfile,threading,time
from Benchmark import ROOT,SIZES,PROFILES,metadata
sys.path.insert(0,str(ROOT/'tests'))
from PerformanceServer import ControlledServer,expected_sha256,file_sha256,local_environment

def write_report(document,base):
    temp=base.with_suffix('.json.tmp');temp.write_text(json.dumps(document,indent=2)+'\n',encoding='utf-8');temp.replace(base.with_suffix('.json'))
    rows=document['samples']
    if not rows:return
    columns=list(dict.fromkeys(key for row in rows for key in row))
    with base.with_suffix('.csv').open('w',newline='',encoding='utf-8') as output:
        writer=csv.DictWriter(output,fieldnames=columns);writer.writeheader();writer.writerows(rows)
    groups={}
    for row in rows:groups.setdefault((row['size_label'],row['profile'],row['mode']),{}).setdefault(row['build'],[]).append(row)
    lines=['# Phase 8 paired local benchmarks','',f"Completed: {document['complete']}. Independently verified samples: {len(rows)}.",'',
        'Release builds alternate in a deterministic shuffled order on the same server. Three repetitions by default. Decimal MB/GB; MiB/s rates. OS caches stay enabled. Both clients perform the same full SHA-256 verification. This is controlled HTTP/1.1 loopback, not Internet, physical-disk, or IDM performance.','',
        '| Size | Server | Mode | Pairs | Baseline MiB/s | Optimized MiB/s | Median paired rate change | Baseline CPU s | Optimized CPU s | Optimized peak MiB |',
        '|---|---|---|---:|---:|---:|---:|---:|---:|---:|']
    comparisons=[]
    for key,builds in groups.items():
        old,new=builds.get('baseline',[]),builds.get('optimized',[])
        if not old or not new:continue
        old_by_rep={row['repetition']:row for row in old};new_by_rep={row['repetition']:row for row in new}
        repetitions=sorted(old_by_rep.keys()&new_by_rep.keys())
        if not repetitions:continue
        old=[old_by_rep[rep] for rep in repetitions];new=[new_by_rep[rep] for rep in repetitions]
        changes=[(new_by_rep[rep]['end_to_end_mib_s']/old_by_rep[rep]['end_to_end_mib_s']-1)*100 for rep in repetitions]
        change=statistics.median(changes)
        comparison={'size':key[0],'profile':key[1],'mode':key[2],'pairs':len(repetitions),'paired_rate_changes_percent':changes,'median_paired_change_percent':change}
        comparisons.append(comparison)
        lines.append('| '+ ' | '.join([*key,str(len(repetitions)),f"{statistics.median(r['end_to_end_mib_s'] for r in old):.2f}",f"{statistics.median(r['end_to_end_mib_s'] for r in new):.2f}",f'{change:+.1f}%',f"{statistics.median(r['cpu_seconds'] for r in old):.3f}",f"{statistics.median(r['cpu_seconds'] for r in new):.3f}",f"{max(r['peak_working_set_bytes'] for r in new)/1048576:.2f}"])+' |')
    document['comparisons']=comparisons
    temp.write_text(json.dumps(document,indent=2)+'\n',encoding='utf-8');temp.replace(base.with_suffix('.json'))
    lines+=['','All files are reread for size/SHA-256 after client completion. Exact contiguous nonoverlapping range coverage, byte totals, and connection limits must pass before acceptance. Process wall time includes native startup/checksum/flush/shutdown and excludes the independent reread. CPU/memory are native process measurements; write calls are cached Windows I/O.','',
        'Per-connection cap: 4 MiB/s; shared cap: 16 MiB/s; shaped profiles have 25 ms header delay. Large files use unlimited traffic unless explicitly requested. Fixed modes use 4 MiB chunks; auto uses adaptive chunks in both builds. Partial reports retain complete pairs only for comparisons. Three repetitions do not establish statistical significance; inspect individual paired changes and spread.','']
    base.with_suffix('.md').write_text('\n'.join(lines),encoding='utf-8')

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--baseline',type=pathlib.Path,default=ROOT/'build/phase8-baseline/Release/DownshiftDownload.exe')
    parser.add_argument('--optimized',type=pathlib.Path,default=ROOT/'build/windows-msvc/Release/DownshiftDownload.exe')
    parser.add_argument('--baseline-source',type=pathlib.Path,default=ROOT/'benchmarks/results/phase8-baseline-source.json')
    parser.add_argument('--sizes',nargs='+',choices=SIZES,default=list(SIZES))
    parser.add_argument('--profiles',nargs='+',choices=PROFILES,default=list(PROFILES))
    parser.add_argument('--modes',nargs='+',choices=['1','4','8','16','auto','stream'],default=['1','4','8','16','auto','stream'])
    parser.add_argument('--repetitions',type=int,default=3)
    parser.add_argument('--shape-large-files',action='store_true')
    parser.add_argument('--output',type=pathlib.Path,required=True)
    args=parser.parse_args()
    if args.repetitions<1:parser.error('Positive repetition count required')
    base=args.output.resolve();base.parent.mkdir(parents=True,exist_ok=True)
    if base.with_suffix('.json').exists():parser.error('Choose a fresh output name; evidence is never overwritten')
    clients={'baseline':args.baseline.resolve(),'optimized':args.optimized.resolve()}
    archive=json.loads(args.baseline_source.read_text(encoding='utf-8'))
    for file,digest in archive['metadata']['source_sha256'].items():
        assert hashlib.sha256(base64.b64decode(archive['file_bytes_base64'][file])).hexdigest()==digest,file
    assert file_sha256(clients['baseline'])==archive['metadata']['executable_sha256'],'Baseline binary does not match preserved evidence'
    current=metadata(clients['optimized']);current['source_sha256']['scripts/BenchmarkPhase8.py']=file_sha256(pathlib.Path(__file__))
    powershell=pathlib.Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
    state=subprocess.run([str(powershell),'-NoProfile','-Command','Get-CimInstance Win32_Processor | Select-Object CurrentClockSpeed,MaxClockSpeed,LoadPercentage | ConvertTo-Json -Compress'],capture_output=True,text=True,timeout=30)
    current['pre_run_processor_state']=json.loads(state.stdout) if state.returncode==0 else None
    document={'complete':False,'metadata':{'baseline':archive['metadata'],'optimized':current,'baseline_archive_sha256':file_sha256(args.baseline_source),'comparison':'Paired old/new runs on one server; both builds verify full SHA-256; controlled loopback'},'settings':{'sizes':args.sizes,'profiles':args.profiles,'modes':args.modes,'repetitions':args.repetitions,'seed':20261012,'shape_large_files':args.shape_large_files},'samples':[],'failed_attempts':[]}
    write_report(document,base)
    server=ControlledServer();worker=threading.Thread(target=server.serve_forever,daemon=True);worker.start()
    randomizer=random.Random(20261012)
    attempt=None
    try:
        scratch=ROOT/'build/benchmark-files';scratch.mkdir(parents=True,exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='phase8-',dir=scratch) as folder:
            for label in args.sizes:
                size=SIZES[label]
                if shutil.disk_usage(folder).free<size+1024**3:raise RuntimeError('Insufficient space')
                expected=expected_sha256(size)
                for profile in args.profiles:
                    if size>SIZES['100MB'] and profile!='unlimited' and not args.shape_large_files:continue
                    for repetition in range(1,args.repetitions+1):
                        modes=args.modes.copy();randomizer.shuffle(modes)
                        for mode in modes:
                            builds=list(clients);randomizer.shuffle(builds)
                            for build in builds:
                                # A distinct destination prevents any stale metadata/name
                                # from a previous sample becoming part of the next sample.
                                target=pathlib.Path(folder)/f'{label}-{profile}-{mode}-{repetition}-{build}.bin'
                                attempt={'build':build,'size_label':label,'profile':profile,'mode':mode,'repetition':repetition,'destination_name':target.name}
                                # Independent rereads and cleanup finish before the next timed sample.
                                deadline=time.monotonic()+5
                                while server.active and time.monotonic()<deadline:time.sleep(.01)
                                server.configure(size,**PROFILES[profile]);count='auto' if mode=='stream' else mode
                                command=[str(clients[build]),f'http://127.0.0.1:{server.server_port}/'+('stream' if mode=='stream' else 'range'),str(target),'--connections',count,'--sha256',expected,'--json']
                                if mode not in ('auto','stream'):command+=['--chunk-kib','4096']
                                print(f'RUN {label} {profile} {mode} {repetition} {build}',flush=True)
                                began=time.perf_counter();result=subprocess.run(command,capture_output=True,text=True,env=local_environment(),timeout=1800);wall=time.perf_counter()-began
                                attempt.update(return_code=result.returncode,wall_seconds=wall,stderr=result.stderr)
                                if result.returncode:raise RuntimeError(result.stderr)
                                metrics=json.loads(result.stdout)
                                attempt['metrics']=metrics
                                if metrics.get('cleanup_pending'):raise RuntimeError('Recovery journal cleanup remained pending')
                                if target.stat().st_size!=size or file_sha256(target)!=expected:raise RuntimeError('Independent checksum/size mismatch')
                                if metrics['bytes']!=size or metrics['network_bytes']!=size or metrics['write_bytes']!=size:raise RuntimeError('Wrong payload byte totals')
                                ranges=sorted((r['first'],r['last']) for r in server.records if not r['probe'])
                                if not ranges or ranges[0][0]!=0 or ranges[-1][1]!=size-1 or any(a[1]+1!=b[0] for a,b in zip(ranges,ranges[1:])):raise RuntimeError('Coverage gap/overlap')
                                limit=16 if count=='auto' else int(count)
                                if server.peak>limit or metrics['peak_active_ranges']>limit:raise RuntimeError('Connection limit exceeded')
                                row={'build':build,'size_label':label,'size_bytes':size,'profile':profile,'mode':mode,'repetition':repetition,'sha256':expected,'sha256_verified':True,'wall_seconds':wall,'end_to_end_mib_s':size/wall/1048576,'server_peak_active':server.peak,**metrics}
                                document['samples'].append(row);write_report(document,base);target.unlink()
                                attempt=None
                                print(f"PASS {build} {row['end_to_end_mib_s']:.2f} MiB/s CPU={metrics['cpu_seconds']:.3f}s",flush=True)
        document['complete']=True
    except Exception as error:
        document['failed_attempts'].append({**(attempt or {}),'failure':str(error)})
        raise
    finally:
        server.shutdown();server.server_close();worker.join(2)
        document['finished_utc']=datetime.datetime.now(datetime.timezone.utc).isoformat();write_report(document,base)
    print(f"{len(document['samples'])} independently verified paired samples saved",flush=True)
if __name__=='__main__':main()
