import assert from "node:assert/strict";
import {HandoffController,eligible,safeFilename} from "../extension/handoff.js";
let checks=0;
function fixture(options={}) {
  const f={saved:{enabled:true,pending:{},bypass:[]},item:{id:1,state:"in_progress",paused:false,url:"https://files.example/file.bin",filename:"C:\\Downloads\\file.bin",totalBytes:1048576,mime:"application/octet-stream",danger:"safe"},receipt:"unknown",log:[],commits:0,restarts:0,offline:false};
  f.api={strict:false,chooseLocation:false,ownId:"own",key:()=>"0123456789abcdef0123456789abcdef",load:async()=>structuredClone(f.saved),save:async state=>{f.saved=structuredClone(state);assert(!JSON.stringify(state).includes("https://"));},item:async()=>structuredClone(f.item),
    pause:async()=>{f.log.push("pause");f.item.paused=true;},resume:async()=>{f.log.push("resume");f.item.paused=false;},cancel:async()=>{assert.equal(f.receipt,"prepared");f.log.push("cancel");if(options.cancelFails)throw Error();f.item.state="interrupted";f.item.error="USER_CANCELED";},restart:async()=>{f.log.push("restart");f.restarts++;},
    native:async request=>{
      f.log.push(request.command);if(f.offline || options.missing)throw Error();let state=f.receipt;
      if(request.command==="ping")state="ready";
      if(request.command==="prepare"){
        f.lastPrepare=request;
        assert(options.direct || f.item.paused);if(options.prepareFails)throw Error();if(f.receipt==="unknown")f.receipt="prepared";state=f.receipt;
        if(options.completedDuringPrepare)f.item.state="complete";
        if(options.disableDuringPrepare)f.disablePromise=f.controller.configure(false);
      }
      if(request.command==="commit"){
        if(options.direct){assert.equal(request.browserNotStarted,true);assert.equal(request.browserCancelled,undefined);}else{assert.equal(f.item.error,"USER_CANCELED");assert.equal(request.browserCancelled,true);}
        if(options.offlineAtCommit && !f.recovered){f.offline=true;throw Error();}
        if(f.receipt==="prepared"){f.receipt="committed";f.commits++;}state=f.receipt;
        if(options.lostCommit && !f.lost){f.lost=true;throw Error();}
      }
      if(request.command==="abort"){if(f.receipt==="prepared")f.receipt="aborted";state=f.receipt;}
      return {ok:true,version:1,state,downloadId:"native-id"};
    }};
  f.controller=new HandoffController(f.api);return f;
}
const check=(value,message)=>{assert(value,message);checks++;};
{
  const f=fixture();await f.controller.candidate(f.item);check(f.receipt==="committed" && f.commits===1,"Normal handoff");check(f.log.indexOf("prepare")<f.log.indexOf("cancel"),"Prepare before browser cancel");check(f.log.indexOf("cancel")<f.log.indexOf("commit"),"Cancel before native commit");await f.controller.reconcile();check(f.commits===1 && !Object.keys(f.saved.pending).length,"Idempotent completion");
}
for(const options of [{missing:true},{prepareFails:true},{cancelFails:true},{completedDuringPrepare:true}]){
  const f=fixture(options);await f.controller.candidate(f.item);check(f.commits===0,"Failure never commits");check(!f.item.paused || f.item.state==="complete","Browser preserved/resumed");
}
{
  const f=fixture({lostCommit:true});await f.controller.candidate(f.item);check(f.commits===1 && Object.keys(f.saved.pending).length===1,"Lost acknowledgment retained");f.controller=new HandoffController(f.api);await f.controller.reconcile();check(f.commits===1 && !Object.keys(f.saved.pending).length,"Worker restart deduplicates committed receipt");
}
{
  const f=fixture();f.receipt="prepared";f.item.paused=true;f.saved.pending[1]={key:f.api.key(),phase:"preparing",keep:false};await f.controller.reconcile();check(f.commits===1 && f.item.error==="USER_CANCELED","Restart during preparation reconciles");
}
{
  const f=fixture({offlineAtCommit:true});await f.controller.candidate(f.item);check(f.commits===0 && f.saved.pending[1].phase==="cancelled","Outage after cancel remains pending");await f.controller.leave();check(f.restarts===0,"No uncertain duplicate fallback");f.offline=false;f.recovered=true;f.controller=new HandoffController(f.api);await f.controller.reconcile();check(f.receipt==="aborted" && f.restarts===1,"Acknowledged abort restores Chrome");
}
{
  const f=fixture({disableDuringPrepare:true});await f.controller.candidate(f.item);await f.disablePromise;check(f.receipt==="aborted" && !f.item.paused && !f.log.includes("cancel"),"Disable during preflight keeps Chrome");
}
for(const overrides of [{url:"blob:abc"},{url:"data:abc"},{danger:"dangerous"},{incognito:true},{state:"complete"}])check(!eligible({...fixture().item,...overrides},"own"),"Unsupported browser download rejected");
for(const overrides of [{filename:"installer.exe"},{filename:"script.ps1"},{totalBytes:10}])check(eligible({...fixture().item,...overrides},"own"),"Chrome-safe public files are candidates regardless of extension or size");
for(const options of [{missing:true},{prepareFails:true},{cancelFails:true}]){
 const f=fixture(options);f.api.strict=true;f.controller=new HandoffController(f.api);await f.controller.candidate(f.item);
 check(f.commits===0 && f.item.paused,"Strict failure holds browser download");check(!f.log.includes("resume") && !f.restarts,"No Chrome fallback while enabled");
 if(!options.missing){await f.controller.configure(false);check(!f.item.paused,"Explicit off restores Chrome");}
}
{const f=fixture();f.api.strict=true;f.controller=new HandoffController(f.api);f.item.url="blob:unsupported";await f.controller.candidate(f.item);check(f.item.paused && f.saved.pending[1].phase==="blocked","Browser-only downloads held with explicit error");await f.controller.configure(false);check(!f.item.paused,"Off restores unsupported browser-only download");}
{const f=fixture();f.api.erase=async()=>{assert.equal(f.receipt,'committed');f.log.push('erase');};await f.controller.candidate(f.item);check(f.log.indexOf('erase')>f.log.indexOf('commit'),'Erase only after acknowledged native commit');check(f.saved.cleanup.length===0,'Cleanup receipt drained');}
{const f=fixture({lostCommit:true});let erased=0;f.api.erase=async()=>{erased++;};await f.controller.candidate(f.item);check(erased===0,'Uncertain commit retains Chrome recovery');await f.controller.reconcile();check(erased===1 && f.commits===1,'Recovery confirms ownership before erasing');}
{const f=fixture({direct:true});await f.controller.direct('https://files.example/file.bin','file.bin');check(f.commits===1 && !f.log.includes('pause') && !f.log.includes('cancel'),'Direct link never starts or cancels Chrome');check(f.saved.direct.length===0,'Direct receipt finished');}
{const f=fixture({direct:true,lostCommit:true});await f.controller.direct('https://files.example/file.bin','file.bin');check(f.commits===1 && f.saved.direct.length===0,'Direct lost acknowledgment reconciles without duplicate');}
{const f=fixture({direct:true,prepareFails:true});await assert.rejects(f.controller.direct('https://files.example/file.bin','file.bin'));check(f.commits===0 && f.saved.direct.length===0 && !f.log.includes('cancel'),'Failed direct preparation leaves browser untouched');}
{const f=fixture({direct:true});f.api.ensureApp=async()=>({capabilities:{serverFilename:true}});await f.controller.direct('https://files.example/download','download');check(f.lastPrepare.serverFilename===true,'Native server filename requested for a default link name');}
{const f=fixture({direct:true});f.api.ensureApp=async()=>({capabilities:{serverFilename:true}});await f.controller.direct('https://files.example/download','custom.bin',false);check(f.lastPrepare.serverFilename===undefined,'Explicit user filename is preserved');}
{
 const f=fixture({prepareFails:true});f.api.strict=true;f.controller=new HandoffController(f.api);await f.controller.candidate(f.item);
 f.item.paused=false;await f.controller.reconcile();check(f.item.paused,'A blocked download resumed externally is immediately held again');
 check(!f.log.includes('resume') && !f.restarts,'Reconciliation cannot fall back while enabled');
}
{
 const f=fixture({prepareFails:true});f.api.strict=true;f.controller=new HandoffController(f.api);await f.controller.candidate(f.item);
 f.item.paused=false;await f.controller.candidate(f.item);check(f.item.paused,'A Chrome resume event holds an existing blocked record');
}
{
 const f=fixture();f.api.strict=true;f.saved.bypass=[1];f.item.byExtensionId='own';f.controller=new HandoffController(f.api);
 await f.controller.candidate(f.item);check(f.commits===1,'Old bypass and extension initiator do not exempt an enabled HTTP download');
}
{
 const f=fixture();f.api.strict=true;f.api.active=async()=>[structuredClone(f.item)];f.controller=new HandoffController(f.api);
 await f.controller.reconcile();check(f.commits===1,'Worker startup discovers a live download missing from extension state');
}
{
 const f=fixture();let now=100;f.api.strict=true;f.api.now=()=>now;f.api.scheduleRetry=when=>{f.scheduled=when;};f.offline=true;f.controller=new HandoffController(f.api);
 await f.controller.candidate(f.item);check(f.item.paused && f.scheduled===2100,'Native startup failure schedules a bounded retry while holding Chrome');
 f.offline=false;now=2100;await f.controller.reconcile();check(f.commits===1 && !f.log.includes('resume'),'Transient native failure recovers automatically without Chrome fallback');
}
for(const danger of ['accepted','allowlistedByPolicy','deepScannedSafe'])check(eligible({...fixture().item,danger},'own'),'Chrome-approved safe verdict can hand off');
{
 const f=fixture();f.api.strict=true;f.item.filename='';f.controller=new HandoffController(f.api);
 await f.controller.candidate({...f.item,filename:'C:\\Downloads\\real-name.rbxmx'});
 check(f.lastPrepare.filename==='real-name.rbxmx','Filename event hint survives an empty Chrome search filename');
 check(!JSON.stringify(f.saved).includes('real-name'),'Filename hints do not enter persistent extension state');
}
check(safeFilename('a'.repeat(200)+'.rbxmx').endsWith('.rbxmx') && safeFilename('a'.repeat(200)+'.rbxmx').length===160,'Long names retain the file extension within native limits');
check(safeFilename('C:\\folder\\bad:name?.exe')==='bad_name_.exe','Filename path and Windows-invalid characters are sanitized');
check(safeFilename('CON.exe')==='_CON.exe' && safeFilename('...')==='download.bin','Reserved and empty Windows names stay usable');
{
 const f=fixture();f.api.strict=true;f.receipt='committed';f.saved.pending[1]={key:f.api.key(),phase:'cancelled',keep:false};
 f.api.cancel=async()=>{f.log.push('cancel');f.item.state='interrupted';f.item.error='USER_CANCELED';};
 f.api.erase=async()=>{check(f.item.state==='interrupted','A recovered committed receipt cannot erase a live Chrome download');f.log.push('erase');};
 f.controller=new HandoffController(f.api);await f.controller.reconcile();check(f.log.includes('cancel') && f.log.indexOf('cancel')<f.log.indexOf('erase'),'Recovered committed handoff stops externally resumed Chrome before erasing');
}
{
 const f=fixture();f.api.strict=true;f.item.state='complete';f.api.browserCapture=async(id,record,item)=>{f.bridgeItem=item;};f.controller=new HandoffController(f.api);
 await f.controller.candidate(f.item,'filename');check(f.bridgeItem?.state==='complete' && !f.log.includes('prepare'),'A tiny already-complete response with a fresh filename event is adopted without another HTTP fetch');
}
for(const event of ['change','created']){
 const f=fixture();f.api.strict=true;f.item.state='complete';f.item.startTime='2025-08-14T05:17:36Z';let imports=0;f.api.browserCapture=async()=>{imports++;};f.controller=new HandoffController(f.api);
 await f.controller.candidate(f.item,event);check(imports===0 && f.log.length===0 && !Object.keys(f.saved.pending).length,'Historical completion/restoration metadata cannot open a native prompt');
}
{
 const f=fixture();f.api.strict=true;f.item.state='complete';f.item.startTime=new Date().toISOString();let imports=0;f.api.browserCapture=async()=>{imports++;};f.controller=new HandoffController(f.api);
 await f.controller.candidate(f.item,'created');check(imports===1,'A genuinely new fast completion is still accepted from onCreated');
 await f.controller.serial(()=>f.controller.finish('1','Saved'));f.controller=new HandoffController(f.api);
 await f.controller.candidate(f.item,'filename');check(imports===1,'A delayed duplicate event after worker restart cannot recapture a finished response');
}
{
 const f=fixture();f.api.strict=true;f.item.state='complete';f.item.startTime='2025-08-14T05:17:36Z';f.saved.pending[1]={key:f.api.key(),phase:'browser-streaming',transport:'browser'};let imports=0;f.api.browserCapture=async()=>{imports++;};f.controller=new HandoffController(f.api);
 await f.controller.candidate(f.item);check(imports===1,'A durable owned receipt still recovers an older completed response');
}
{
 const f=fixture();f.api.strict=true;f.saved.pending[1]={key:'f'.repeat(32),phase:'blocked',attempts:5};f.api.browserCapture=async(id,record)=>{f.bridgeKey=record.key;};f.controller=new HandoffController(f.api);
 await f.controller.reconcile();check(f.bridgeKey && f.bridgeKey!=='f'.repeat(32),'An old blocked handoff upgrades to the response bridge instead of remaining stuck');
}
{
 const f=fixture();f.api.strict=true;f.saved.pending[1]={key:f.api.key(),phase:'browser-streaming',transport:'browser'};f.api.browserCapture=async()=>{throw Error('offline');};f.controller=new HandoffController(f.api);
 await f.controller.reconcile();check(f.item.paused && !f.log.includes('resume'),'An app outage holds its response transport without browser fallback');
}
console.log(`Extension state checks passed: ${checks}`);
