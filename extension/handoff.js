// Durable stages contain IDs and decisions only. URLs remain in Chrome's own download metadata.
export function safeFilename(value) {
  let name=String(value || "download.bin").split(/[\\/]/).pop().replace(/[<>:"|?*\x00-\x1f]/g,"_").trim().replace(/[. ]+$/g,"");
  if(!name || name==="." || name==="..")name="download.bin";
  if(/^(?:con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(name))name="_"+name;
  if(name.length>160){const suffix=name.match(/\.[^.]{1,20}$/)?.[0] || "";let stem=name.slice(0,160-suffix.length);if(/[\uD800-\uDBFF]$/.test(stem))stem=stem.slice(0,-1);name=stem+suffix;}
  return name;
}
export function eligible(item, ownId) {
  const url = item.finalUrl || item.url || "";
  if (item.state !== "in_progress" || item.incognito ||
      (item.danger && !["safe","accepted","allowlistedByPolicy","deepScannedSafe"].includes(item.danger)) || !/^https?:\/\//i.test(url)) return false;
  const name = (item.filename || new URL(url).pathname).split(/[\\/]/).pop();
  return !!name;
}
export class HandoffController {
  constructor(api) { this.api = api; this.strict=api.strict!==false;this.state = null; this.chain = Promise.resolve(); this.requestedEnabled = undefined;this.filenameHints=new Map();this.startedAt=api.now?.() ?? Date.now(); }
  serial(action) { const next = this.chain.then(async () => { await this.load(); return action(); }); this.chain = next.catch(() => {}); return next; }
  async load() { if (!this.state) { const saved = await this.api.load(); this.state = {enabled:true,pending:{},bypass:[],notice:"",...saved}; } }
  async save() { await this.api.save(this.state); }
  async native(command, values={}) { const answer = await this.api.native({version:1,command,...values}); if (!answer || answer.version !== 1 || !answer.ok) throw new Error(answer?.version===1 && typeof answer.error==='string' ? answer.error.slice(0,240) : "Downshift could not accept this request. Check the extension for recovery options."); return answer; }
  async rememberBypass(id) { if (!this.state.bypass.includes(id)) this.state.bypass.push(id); this.state.bypass = this.state.bypass.slice(-1000); await this.save(); }
  async finish(id, notice) { delete this.state.pending[id];this.filenameHints.delete(Number(id));this.state.finished=[...new Set([...(this.state.finished || []),Number(id)])].slice(-1000); this.state.notice = notice; await this.save(); }
  async handed(id) {
    // A committed receipt survives worker restarts. If Chrome was resumed in the
    // meantime, stop it before erasing history; erase alone does not stop bytes.
    if(this.strict && this.state.enabled){
      let item=await this.api.item(Number(id));
      if(item?.state==="in_progress"){
        if(!item.paused)await this.api.pause(Number(id));
        await this.api.cancel(Number(id));item=await this.api.item(Number(id));
        if(item?.state!=="interrupted" || item.error!=="USER_CANCELED")throw Error("Chrome cancellation was not verified");
      }
    }
    if(this.api.erase)this.state.cleanup=[...new Set([...(this.state.cleanup || []),Number(id)])];
    await this.finish(id,"Handed to Downshift.");
    await this.cleanHistory();
  }
  async cleanHistory() {
    if(!this.api.erase)return;
    for(const id of this.state.cleanup || [])try{await this.api.erase(id);this.state.cleanup=this.state.cleanup.filter(value=>value!==id);await this.save();}catch{}
  }
  direct(url,filename,preferServerFilename=true) {return this.serial(async()=>{
    if(!this.state.enabled)throw Error("Capture is off.");
    if((this.state.direct || []).length>=20)throw Error("Reconnect Downshift to finish pending link requests.");
    const app=this.api.ensureApp ? await this.api.ensureApp():null;
    const key=this.api.key();this.state.direct=[...(this.state.direct || []),key];await this.save();
    try {
      await this.native("prepare",{key,url,filename,total:"-1",mime:"",...(preferServerFilename && app?.capabilities?.serverFilename ? {serverFilename:true}:{})});
      const answer=await this.native("commit",{key,browserNotStarted:true,prompt:true});
      if(answer.state!=="committed")throw Error("Commit not acknowledged");
      this.state.direct=this.state.direct.filter(value=>value!==key);this.state.notice="Opened in Downshift.";await this.save();return answer;
    }catch(error){
      try{const answer=await this.native("status",{key});if(answer.state==="committed"){this.state.direct=this.state.direct.filter(value=>value!==key);await this.save();return answer;}await this.native("abort",{key});this.state.direct=this.state.direct.filter(value=>value!==key);await this.save();}catch{}
      throw error;
    }
  });}
  async reconcileDirect() {
    for(const key of this.state.direct || [])try{const answer=await this.native("status",{key});if(answer.state==="prepared")await this.native("abort",{key});this.state.direct=this.state.direct.filter(value=>value!==key);await this.save();}catch{}
  }
  async keep(id, error) {
    const record = this.state.pending[id]; if (!record) return;
    if(record.transport==='browser'){
      if(!this.state.enabled && !record.browserCommitSent){try{await this.native('abort',{key:record.key});}catch{}const item=await this.api.item(Number(id));if(item?.state==='in_progress' && item.paused)await this.api.resume(Number(id));await this.finish(id,'Capture is off.');return;}
      await this.api.browserCapture(id,record,await this.api.item(Number(id)),this);return;
    }
    if(this.strict && this.state.enabled){
      const item=await this.api.item(Number(id));
      if(item?.state!=="in_progress" && record.phase!=="cancelling" && record.phase!=="cancelled"){try{await this.native("abort",{key:record.key});}catch{return;}await this.finish(id,"Chrome completed or cancelled this download before handoff.");return;}
      if(item?.state==="in_progress" && !item.paused)await this.api.pause(Number(id));
      record.phase="blocked";record.keep=false;
      record.attempts=(record.attempts || 0)+1;record.retryAt=(this.api.now?.() ?? Date.now())+Math.min(60000,2000*2**Math.min(record.attempts-1,5));
      if(record.attempts<5)this.api.scheduleRetry?.(record.retryAt);
      const reason=String(error?.message || "Downshift could not accept the file yet.").replace(/(?:https?:\/\/|blob:)[^\s]+/gi,"[address]").slice(0,180);
      this.state.notice=`Held for Downshift. ${reason} Chrome will not resume while capture is on.`;await this.save();try{await this.native("abort",{key:record.key});await this.native("blocked");}catch{}return;
    }
    record.keep = true; await this.save();
    let receipt;
    try { receipt = await this.native("abort", {key:record.key}); }
    catch {if(!this.state.enabled && record.phase!=="cancelling" && record.phase!=="cancelled"){const item=await this.api.item(Number(id));if(item?.state==="in_progress" && item.paused)try{await this.api.resume(Number(id));}catch{}} this.state.notice = "Reconnect Downshift to safely restore or finish this pending download."; await this.save(); return; }
    if (receipt.state === "committed") { await this.finish(id,"Already handed to Downshift. Manage this file in the app."); return; }
    if (receipt.state !== "aborted" && receipt.state !== "unknown") return;
    const item = await this.api.item(Number(id)); await this.rememberBypass(Number(id));
    if (item?.state === "in_progress" && item.paused) {
      try { await this.api.resume(Number(id)); } catch { this.state.notice="Chrome could not resume this download. Retry it from Chrome."; await this.save(); return; }
    } else if (item?.state === "interrupted" && item.error === "USER_CANCELED" && (record.phase === "cancelling" || record.phase === "cancelled")) {
      // Abort acknowledgment proves native work has not been committed. Restart keeps Chrome cookies.
      try { await this.api.restart(item); } catch { this.state.notice="Chrome could not restart this link. Retry it from its original page."; await this.save(); return; }
    }
    await this.finish(id,"Download left in Chrome.");
  }
  async run(id) {
    const record=this.state.pending[id]; if(!record) return;
    if(record.transport==='browser'){
      try{await this.api.browserCapture(id,record,await this.api.item(Number(id)),this);}
      catch{const item=await this.api.item(Number(id));if(item?.state==='in_progress' && !item.paused)try{await this.api.pause(Number(id));}catch{}this.state.notice='Connecting your download to Downshift…';await this.save();}
      return;
    }
    // A paused download can be resumed from Chrome, by another extension or after
    // browser restart. Enforce the hold BEFORE considering an already-blocked row.
    if(this.strict && this.state.enabled){const item=await this.api.item(Number(id));if(item?.state==="in_progress" && !item.paused)await this.api.pause(Number(id));}
    if(record.phase==="blocked" && this.state.enabled){
      if(this.api.browserCapture){record.key=this.api.key();record.transport='browser';record.phase='browser-preparing';record.keep=false;await this.save();await this.run(id);return;}
      if((record.attempts || 0)>=5 || (this.api.now?.() ?? Date.now())<(record.retryAt || 0))return;
      record.key=this.api.key();record.phase="created";record.keep=false;await this.save();
    }
    if(record.keep || !this.state.enabled) {await this.keep(id);return;}
    try {
      let item=await this.api.item(Number(id));
      if(this.strict && item?.state==="in_progress") {
        if(!item.paused){record.phase="pausing";await this.save();await this.api.pause(Number(id));}
        if(this.api.ensureApp)await this.api.ensureApp();
      }
      let receipt=await this.native("status",{key:record.key});
      if(receipt.state==="committed") {await this.handed(id);return;}
      if(receipt.state==="aborted") {record.keep=true;await this.keep(id);return;}
      if(this.api.browserCapture && item?.state==='complete'){
        if(receipt.state==='prepared'){await this.native('abort',{key:record.key});record.key=this.api.key();}
        record.transport='browser';record.phase='browser-preparing';record.keep=false;await this.save();await this.api.browserCapture(id,record,item,this);return;
      }
      if(this.api.browserCapture && item && receipt.state==='unknown' && (!eligible(item,this.api.ownId) || !this.api.sourceTag?.(item))){
        record.transport='browser';record.phase='browser-preparing';record.keep=false;await this.save();await this.api.browserCapture(id,record,item,this);return;
      }
      if(!item || item.state==="complete") {record.keep=true;await this.keep(id);return;}
      if(item.state==="interrupted" && item.error==="USER_CANCELED" && (record.phase==="cancelling" || record.phase==="cancelled")) {
        if(receipt.state==="prepared") {await this.native("commit",{key:record.key,browserCancelled:true,prompt:this.api.chooseLocation!==false});await this.handed(id);return;}
        record.keep=true;await this.keep(id);return;
      }
      if(!eligible(item,this.api.ownId)) {
        const url=item.finalUrl || item.url || "";
        throw Error(url.startsWith("blob:") ? "This file exists only in its browser page (blob); it has no HTTPS download address." : item.incognito ? "Private-window handoff is unavailable." : "Chrome has not approved this file or it has no supported HTTP/HTTPS address.");
      }
      if(!item.paused) {await this.native("ping");record.phase="pausing";await this.save();await this.api.pause(Number(id));}
      record.phase="paused";await this.save();item=await this.api.item(Number(id));
      // search().filename is empty until Chrome finishes filename determination.
      // Its event supplies the proposed filename even while our gate holds it.
      const proposed=item.filename || this.filenameHints.get(Number(id));
      let fallback=new URL(item.finalUrl || item.url).pathname.split('/').pop();try{fallback=decodeURIComponent(fallback);}catch{}
      const filename=safeFilename(proposed || fallback);
      record.phase="preparing";await this.save();receipt=await this.native("prepare",{key:record.key,url:item.finalUrl || item.url,filename,total:String(item.totalBytes ?? -1),mime:String(item.mime || "").slice(0,120),...(!proposed ? {serverFilename:true}:{}),...(this.api.sourceTag?.(item) ? {sourceEtag:this.api.sourceTag(item)}:{})});
      if(receipt.state!=="prepared") {if(receipt.state==="committed"){await this.handed(id);return;}throw new Error("Not accepted");}
      record.phase="prepared";await this.save();
      if(this.requestedEnabled===false){this.state.enabled=false;record.keep=true;await this.keep(id);return;}
      item=await this.api.item(Number(id));if(item?.state!=="in_progress" || !item.paused || !eligible(item,this.api.ownId))throw new Error("Chrome changed");
      record.phase="cancelling";await this.save();await this.api.cancel(Number(id));
      item=await this.api.item(Number(id));
      if(item?.state!=="interrupted" || item.error!=="USER_CANCELED")throw new Error("Chrome cancellation was not verified");
      record.phase="cancelled";await this.save();
      if(this.requestedEnabled===false){this.state.enabled=false;record.keep=true;await this.keep(id);return;}
      receipt=await this.native("commit",{key:record.key,browserCancelled:true,prompt:this.api.chooseLocation!==false});if(receipt.state!=="committed")throw new Error("Commit not acknowledged");
      await this.handed(id);
    }catch(error) {
      if(record.phase==="cancelling" || record.phase==="cancelled") {
        const item=await this.api.item(Number(id));
        if(record.phase==="cancelling" && (item?.state==="in_progress" || item?.state==="complete")){record.keep=true;await this.save();await this.keep(id);return;}
        this.state.notice="Waiting for Downshift to confirm a pending handoff. Reconnect the app to finish or restore it.";await this.save();
      }else {
        if(this.strict && this.state.enabled && this.api.browserCapture){
          const item=await this.api.item(Number(id));
          if(item?.state==='in_progress'){
            try{await this.native('abort',{key:record.key});}catch{}
            record.key=this.api.key();record.transport='browser';record.phase='browser-preparing';record.keep=false;await this.save();
            try{await this.api.browserCapture(id,record,item,this);}catch{this.state.notice='Connecting your download to Downshift…';await this.save();}return;
          }
        }
        if(this.strict && this.state.enabled){await this.keep(id,error);return;}
        record.keep=true;await this.save();
        // Before any cancel/commit, a native outage cannot have started the prepared job.
        const item=await this.api.item(Number(id));await this.rememberBypass(Number(id));
        if(item?.state==="in_progress" && item.paused)try{await this.api.resume(Number(id));}catch{}
        if(record.phase==="created" || record.phase==="pausing" || record.phase==="paused"){await this.finish(id,"Download left in Chrome. Open Downshift for future handoffs.");return;}
        await this.keep(id);
      }
    }
  }
  candidate(item, event='change') {
    if(item.filename){this.filenameHints.set(item.id,item.filename);if(this.filenameHints.size>1000)this.filenameHints.delete(this.filenameHints.keys().next().value);}
    const early=this.strict && this.api.captureEnabled && this.state?.pending[item.id]?.transport!=='browser' && item.state==="in_progress" && !item.paused ? this.api.captureEnabled().then(async enabled=>{if(enabled)try{await this.api.pause(item.id);return true;}catch{}return false;}):Promise.resolve(false);
    return this.serial(async()=>{
    const held=await early;if(this.state.pending[item.id]?.transport==='browser'){await this.run(String(item.id));return;}if(!this.state.enabled){if(held)try{await this.api.resume(item.id);}catch{}return;}
    if(!this.state.pending[item.id] && (this.state.finished || []).includes(item.id))return;
    // Chrome may announce restored history or update metadata for files saved
    // long ago. Completion is not evidence of a new download. Adopt a tiny
    // completed response only from its fresh creation/filename event; durable
    // pending receipts still recover independently of this gate.
    const fresh=event==='filename' || (event==='created' && Number.isFinite(Date.parse(item.startTime)) && Date.parse(item.startTime)>=this.startedAt-5000);
    if(item.state==='complete' && !this.state.pending[item.id] && !fresh)return;
    if(!this.state.enabled || (item.state!=="in_progress" && !(this.api.browserCapture && item.state==='complete')))return;
    if(this.state.pending[item.id]){await this.run(String(item.id));return;}
    // Bypass applies only while capture is OFF. A previous explicit Chrome
    // fallback must not exempt a live download after capture is enabled again.
    if(!this.strict && (item.incognito || item.byExtensionId===this.api.ownId || this.state.bypass.includes(item.id)))return;
    if(Object.keys(this.state.pending).length>=20){if(this.strict)try{await this.api.pause(item.id);this.state.overflow=[...new Set([...(this.state.overflow || []),item.id])].slice(-1000);}catch{}this.state.notice="Too many pending handoffs. Downloads are paused; finish existing handoffs or turn the extension off.";await this.save();return;}
    this.state.pending[item.id]={key:this.api.key(),phase:"created",keep:false};await this.save();await this.run(String(item.id));
  });}
  retry(){return this.serial(async()=>{for(const [id,record] of Object.entries(this.state.pending))if(record.phase==="blocked"){record.key=this.api.key();record.phase="created";record.keep=false;record.attempts=0;await this.save();await this.run(id);}});}
  reconcile() {
    // Rediscover live downloads: onCreated may have run in an old worker, or
    // while capture was off. Launch candidate holds before serialized preflight.
    const discovered=this.api.active ? this.api.active().then(items=>Promise.all(items.map(item=>this.candidate(item)))):Promise.resolve();
    return discovered.then(()=>this.serial(async()=>{for(const id of Object.keys(this.state.pending))await this.run(id);await this.reconcileDirect();await this.cleanHistory();}));
  }
  configure(enabled) {this.requestedEnabled=!!enabled;return this.serial(async()=>{this.state.enabled=!!enabled;await this.save();if(!enabled){for(const id of Object.keys(this.state.pending))try{await this.keep(id);}catch{}for(const id of this.state.overflow || [])try{await this.api.resume(id);}catch{}this.state.overflow=[];await this.save();}return this.summary();});}
  summary() {return {enabled:this.state.enabled,pending:Object.keys(this.state.pending).length+(this.state.direct || []).length,notice:this.state.notice};}
  status() {return this.serial(async()=>{let connected=false,appVersion="";try{const app=await this.native("ping");connected=app.state==="ready";appVersion=app.appVersion || "";}catch{}return {...this.summary(),connected,appVersion};});}
  leave() {return this.configure(false);}
}
