import assert from "node:assert/strict";
import {HandoffController,eligible} from "../extension/handoff.js";
let checks=0;
function fixture(options={}) {
  const f={saved:{enabled:true,pending:{},bypass:[]},item:{id:1,state:"in_progress",paused:false,url:"https://files.example/file.bin",filename:"C:\\Downloads\\file.bin",totalBytes:1048576,mime:"application/octet-stream",danger:"safe"},receipt:"unknown",log:[],commits:0,restarts:0,offline:false};
  f.api={ownId:"own",key:()=>"0123456789abcdef0123456789abcdef",load:async()=>structuredClone(f.saved),save:async state=>{f.saved=structuredClone(state);assert(!JSON.stringify(state).includes("https://"));},item:async()=>structuredClone(f.item),
    pause:async()=>{f.log.push("pause");f.item.paused=true;},resume:async()=>{f.log.push("resume");f.item.paused=false;},cancel:async()=>{assert.equal(f.receipt,"prepared");f.log.push("cancel");if(options.cancelFails)throw Error();f.item.state="interrupted";f.item.error="USER_CANCELED";},restart:async()=>{f.log.push("restart");f.restarts++;},
    native:async request=>{
      f.log.push(request.command);if(f.offline || options.missing)throw Error();let state=f.receipt;
      if(request.command==="ping")state="ready";
      if(request.command==="prepare"){
        assert(f.item.paused);if(options.prepareFails)throw Error();if(f.receipt==="unknown")f.receipt="prepared";state=f.receipt;
        if(options.completedDuringPrepare)f.item.state="complete";
        if(options.disableDuringPrepare)f.disablePromise=f.controller.configure(false);
      }
      if(request.command==="commit"){
        assert.equal(f.item.error,"USER_CANCELED");assert.equal(request.browserCancelled,true);
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
for(const overrides of [{url:"blob:abc"},{url:"data:abc"},{filename:"installer.exe"},{filename:"script.ps1"},{mime:"text/html"},{danger:"dangerous"},{incognito:true},{byExtensionId:"own"},{totalBytes:10},{state:"complete"}])check(!eligible({...fixture().item,...overrides},"own"),"Unsupported browser download rejected");
console.log(`Extension state checks passed: ${checks}`);
