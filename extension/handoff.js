// Durable stages contain IDs and decisions only. URLs remain in Chrome's own download metadata.
export function eligible(item, ownId) {
  const url = item.finalUrl || item.url || "";
  if (item.state !== "in_progress" || item.incognito || item.byExtensionId === ownId ||
      (item.danger && item.danger !== "safe") || !/^https?:\/\//i.test(url)) return false;
  const name = (item.filename || new URL(url).pathname).split(/[\\/]/).pop();
  return !!name;
}
export class HandoffController {
  constructor(api) { this.api = api; this.strict=api.strict!==false;this.state = null; this.chain = Promise.resolve(); this.requestedEnabled = undefined; }
  serial(action) { const next = this.chain.then(async () => { await this.load(); return action(); }); this.chain = next.catch(() => {}); return next; }
  async load() { if (!this.state) { const saved = await this.api.load(); this.state = {enabled:true,pending:{},bypass:[],notice:"",...saved}; } }
  async save() { await this.api.save(this.state); }
  async native(command, values={}) { const answer = await this.api.native({version:1,command,...values}); if (!answer || answer.version !== 1 || !answer.ok) throw new Error(answer?.version===1 && typeof answer.error==='string' ? answer.error.slice(0,240) : "Downshift could not accept this request. Check the extension for recovery options."); return answer; }
  async rememberBypass(id) { if (!this.state.bypass.includes(id)) this.state.bypass.push(id); this.state.bypass = this.state.bypass.slice(-1000); await this.save(); }
  async finish(id, notice) { delete this.state.pending[id]; this.state.notice = notice; await this.save(); }
  async handed(id) {
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
  async keep(id) {
    const record = this.state.pending[id]; if (!record) return;
    if(this.strict && this.state.enabled){
      const item=await this.api.item(Number(id));
      if(item?.state!=="in_progress" && record.phase!=="cancelling" && record.phase!=="cancelled"){try{await this.native("abort",{key:record.key});}catch{return;}await this.finish(id,"Chrome completed or cancelled this download before handoff.");return;}
      record.phase="blocked";record.keep=false;this.state.notice="Download paused. Retry the handoff or turn Downshift off to use Chrome.";await this.save();try{await this.native("abort",{key:record.key});await this.native("blocked");}catch{}return;
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
    if(record.phase==="blocked" && this.state.enabled)return;
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
      if(!item || item.state==="complete") {record.keep=true;await this.keep(id);return;}
      if(item.state==="interrupted" && item.error==="USER_CANCELED" && (record.phase==="cancelling" || record.phase==="cancelled")) {
        if(receipt.state==="prepared") {await this.native("commit",{key:record.key,browserCancelled:true,prompt:this.api.chooseLocation!==false});await this.handed(id);return;}
        record.keep=true;await this.keep(id);return;
      }
      if(!eligible(item,this.api.ownId)) {record.keep=true;await this.keep(id);return;}
      if(!item.paused) {await this.native("ping");record.phase="pausing";await this.save();await this.api.pause(Number(id));}
      record.phase="paused";await this.save();item=await this.api.item(Number(id));
      const filename=(item.filename || new URL(item.finalUrl || item.url).pathname).split(/[\\/]/).pop();
      record.phase="preparing";await this.save();receipt=await this.native("prepare",{key:record.key,url:item.finalUrl || item.url,filename,total:String(item.totalBytes ?? -1),mime:String(item.mime || "").slice(0,120)});
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
    }catch {
      if(record.phase==="cancelling" || record.phase==="cancelled") {
        const item=await this.api.item(Number(id));
        if(record.phase==="cancelling" && (item?.state==="in_progress" || item?.state==="complete")){record.keep=true;await this.save();await this.keep(id);return;}
        this.state.notice="Waiting for Downshift to confirm a pending handoff. Reconnect the app to finish or restore it.";await this.save();
      }else {
        if(this.strict && this.state.enabled){await this.keep(id);return;}
        record.keep=true;await this.save();
        // Before any cancel/commit, a native outage cannot have started the prepared job.
        const item=await this.api.item(Number(id));await this.rememberBypass(Number(id));
        if(item?.state==="in_progress" && item.paused)try{await this.api.resume(Number(id));}catch{}
        if(record.phase==="created" || record.phase==="pausing" || record.phase==="paused"){await this.finish(id,"Download left in Chrome. Open Downshift for future handoffs.");return;}
        await this.keep(id);
      }
    }
  }
  candidate(item) {
    const early=this.strict && this.api.captureEnabled && item.state==="in_progress" && !item.incognito && item.byExtensionId!==this.api.ownId ? this.api.captureEnabled().then(async enabled=>{if(enabled)try{await this.api.pause(item.id);return true;}catch{}return false;}):Promise.resolve(false);
    return this.serial(async()=>{
    const held=await early;if(!this.state.enabled){if(held)try{await this.api.resume(item.id);}catch{}return;}
    if(!this.state.enabled || item.state!=="in_progress" || item.incognito || item.byExtensionId===this.api.ownId || this.state.bypass.includes(item.id) || this.state.pending[item.id]) return;
    if(Object.keys(this.state.pending).length>=20){if(this.strict)try{await this.api.pause(item.id);this.state.overflow=[...new Set([...(this.state.overflow || []),item.id])].slice(-1000);}catch{}this.state.notice="Too many pending handoffs. Downloads are paused; finish existing handoffs or turn the extension off.";await this.save();return;}
    this.state.pending[item.id]={key:this.api.key(),phase:"created",keep:false};await this.save();await this.run(String(item.id));
  });}
  retry(){return this.serial(async()=>{for(const [id,record] of Object.entries(this.state.pending))if(record.phase==="blocked"){record.key=this.api.key();record.phase="created";record.keep=false;await this.save();await this.run(id);}});}
  reconcile() {return this.serial(async()=>{for(const id of Object.keys(this.state.pending))await this.run(id);await this.reconcileDirect();await this.cleanHistory();});}
  configure(enabled) {this.requestedEnabled=!!enabled;return this.serial(async()=>{this.state.enabled=!!enabled;await this.save();if(!enabled){for(const id of Object.keys(this.state.pending))await this.keep(id);for(const id of this.state.overflow || [])try{await this.api.resume(id);}catch{}this.state.overflow=[];await this.save();}return this.summary();});}
  summary() {return {enabled:this.state.enabled,pending:Object.keys(this.state.pending).length+(this.state.direct || []).length,notice:this.state.notice};}
  status() {return this.serial(async()=>{let connected=false,appVersion="";try{const app=await this.native("ping");connected=app.state==="ready";appVersion=app.appVersion || "";}catch{}return {...this.summary(),connected,appVersion};});}
  leave() {return this.configure(false);}
}
