"""Bounded installed-file repair checks; all mutations stay in private fixtures."""
import hashlib,json,pathlib,subprocess,uuid
root=pathlib.Path(__file__).resolve().parent.parent
base=root/'build/browser-fixes-20261104/install-fixture';base.mkdir(parents=True,exist_ok=True)
names=['Downshift.exe','build-source.json','extension/app-launcher.js','extension/background.js','extension/file-links.js','extension/handoff.js','extension/media.js','extension/video-button.js']
def digest(data):return hashlib.sha256(data).hexdigest()
checks=0
for mode in ['success','rollback','changed','extra','new-existing']:
 case=base/uuid.uuid4().hex;target=case/'installed';payload=case/'payload';target.mkdir(parents=True);payload.mkdir();(target/'.repair-fixture').write_text('private fixture')
 entries=[];before={}
 for name in names:
  old=target/name;new=payload/name;old.parent.mkdir(parents=True,exist_ok=True);new.parent.mkdir(parents=True,exist_ok=True)
  original=('old '+name).encode();updated=('updated '+name).encode();new.write_bytes(updated)
  if name!='extension/app-launcher.js':old.write_bytes(original);before[name]=original
  entries.append({'path':name,'before':digest(original) if name in before else None,'after':digest(updated)})
 extra=target/'queue.sqlite';extra.write_bytes(b'private history must not change')
 if mode=='changed':(target/'Downshift.exe').write_bytes(b'new external install')
 if mode=='extra':entries.append({'path':'queue.sqlite','before':digest(extra.read_bytes()),'after':digest(b'bad')})
 if mode=='new-existing':(target/'extension/app-launcher.js').write_bytes(b'existing unreviewed module')
 manifest=payload/'repair.json';manifest.write_text(json.dumps({'schema':1,'product':'Downshift','files':entries}))
 args=['powershell.exe','-NoProfile','-ExecutionPolicy','Bypass','-File',str(root/'scripts/Apply-BrowserRepair.ps1'),'-PayloadDirectory',str(payload),'-ManifestSha256',digest(manifest.read_bytes()),'-TargetDirectory',str(target),'-IsolatedFixture']
 if mode=='rollback':args.append('-FailAfterFirstSwap')
 result=subprocess.run(args,capture_output=True,text=True,timeout=30)
 report=json.loads((case/'applied-report.json').read_text(encoding='utf-8-sig'))
 assert extra.read_bytes()==b'private history must not change';checks+=1
 if mode=='success':
  assert result.returncode==0 and report['ok']
  for name in names:assert (target/name).read_bytes()==(payload/name).read_bytes();checks+=1
  for name,value in before.items():assert (pathlib.Path(report['backup'])/name).read_bytes()==value;checks+=1
 else:
  assert result.returncode!=0 and not report['ok'];checks+=1
  if mode=='rollback':
   assert report['rollbackComplete'];assert not (target/'extension/app-launcher.js').exists()
   for name,value in before.items():assert (target/name).read_bytes()==value;checks+=1
  elif mode=='changed':assert (target/'Downshift.exe').read_bytes()==b'new external install';checks+=1
  elif mode=='new-existing':assert (target/'extension/app-launcher.js').read_bytes()==b'existing unreviewed module';checks+=1
print('Browser repair fixture checks passed:',checks)
