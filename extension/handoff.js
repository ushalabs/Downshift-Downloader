// Durable stages contain IDs and decisions only. URLs remain in Chrome's own download metadata.
export function eligible(item, ownId) {
  const url = item.finalUrl || item.url || "";
  if (item.state !== "in_progress" || item.incognito || item.byExtensionId === ownId ||
      (item.danger && item.danger !== "safe") || !/^https?:\/\//i.test(url) || item.totalBytes >= 0 && item.totalBytes < 262144) return false;
  if (/^(text\/html|application\/xhtml)/i.test(item.mime || "")) return false;
  const name = (item.filename || new URL(url).pathname).split(/[\\/]/).pop();
  return !!name && !/\.(exe|msi|bat|cmd|ps1|vbs|js|scr|com|hta|lnk|url|svg|html?)$/i.test(name);
}
export class HandoffController {
  constructor(api) { this.api = api; this.state = null; this.chain = Promise.resolve(); this.requestedEnabled = undefined; }
  serial(action) { const next = this.chain.then(async () => { await this.load(); return action(); }); this.chain = next.catch(() => {}); return next; }
  async load() { if (!this.state) { const saved = await this.api.load(); this.state = {enabled:false,pending:{},bypass:[],notice:"",...saved}; } }
  async save() { await this.api.save(this.state); }
  async native(command, values={}) { const answer = await this.api.native({version:1,command,...values}); if (!answer || answer.version !== 1 || !answer.ok) throw new Error("Downshift could not accept this request. Your download stays in Chrome when possible."); return answer; }
  async rememberBypass(id) { if (!this.state.bypass.includes(id)) this.state.bypass.push(id); this.state.bypass = this.state.bypass.slice(-1000); await this.save(); }
  async finish(id, notice) { delete this.state.pending[id]; this.state.notice = notice; await this.save(); }
  async keep(id) {
    const record = this.state.pending[id]; if (!record) return;
    record.keep = true; await this.save();
    let receipt;
    try { receipt = await this.native("abort", {key:record.key}); }
    catch { this.state.notice = "Reconnect Downshift to safely restore or finish this pending download."; await this.save(); return; }
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
    if(record.keep || !this.state.enabled) {await this.keep(id);return;}
    try {
      let item=await this.api.item(Number(id));
      let receipt=await this.native("status",{key:record.key});
      if(receipt.state==="committed") {await this.finish(id,"Handed to Downshift.");return;}
      if(receipt.state==="aborted") {record.keep=true;await this.keep(id);return;}
      if(!item || item.state==="complete") {record.keep=true;await this.keep(id);return;}
      if(item.state==="interrupted" && item.error==="USER_CANCELED" && (record.phase==="cancelling" || record.phase==="cancelled")) {
        if(receipt.state==="prepared") {await this.native("commit",{key:record.key,browserCancelled:true});await this.finish(id,"Handed to Downshift.");return;}
        record.keep=true;await this.keep(id);return;
      }
      if(!eligible(item,this.api.ownId)) {record.keep=true;await this.keep(id);return;}
      if(!item.paused) {await this.native("ping");record.phase="pausing";await this.save();await this.api.pause(Number(id));}
      record.phase="paused";await this.save();item=await this.api.item(Number(id));
      const filename=(item.filename || new URL(item.finalUrl || item.url).pathname).split(/[\\/]/).pop();
      record.phase="preparing";await this.save();receipt=await this.native("prepare",{key:record.key,url:item.finalUrl || item.url,filename,total:String(item.totalBytes ?? -1)});
      if(receipt.state!=="prepared") {if(receipt.state==="committed"){await this.finish(id,"Handed to Downshift.");return;}throw new Error("Not accepted");}
      record.phase="prepared";await this.save();
      if(this.requestedEnabled===false){this.state.enabled=false;record.keep=true;await this.keep(id);return;}
      item=await this.api.item(Number(id));if(item?.state!=="in_progress" || !item.paused || !eligible(item,this.api.ownId))throw new Error("Chrome changed");
      record.phase="cancelling";await this.save();await this.api.cancel(Number(id));
      item=await this.api.item(Number(id));
      if(item?.state!=="interrupted" || item.error!=="USER_CANCELED")throw new Error("Chrome cancellation was not verified");
      record.phase="cancelled";await this.save();
      if(this.requestedEnabled===false){this.state.enabled=false;record.keep=true;await this.keep(id);return;}
      receipt=await this.native("commit",{key:record.key,browserCancelled:true});if(receipt.state!=="committed")throw new Error("Commit not acknowledged");
      await this.finish(id,"Handed to Downshift.");
    }catch {
      if(record.phase==="cancelling" || record.phase==="cancelled") {
        const item=await this.api.item(Number(id));
        if(record.phase==="cancelling" && (item?.state==="in_progress" || item?.state==="complete")){record.keep=true;await this.save();await this.keep(id);return;}
        this.state.notice="Waiting for Downshift to confirm a pending handoff. Reconnect the app to finish or restore it.";await this.save();
      }else {
        record.keep=true;await this.save();
        // Before any cancel/commit, a native outage cannot have started the prepared job.
        const item=await this.api.item(Number(id));await this.rememberBypass(Number(id));
        if(item?.state==="in_progress" && item.paused)try{await this.api.resume(Number(id));}catch{}
        if(record.phase==="created" || record.phase==="pausing" || record.phase==="paused"){await this.finish(id,"Download left in Chrome. Open Downshift for future handoffs.");return;}
        await this.keep(id);
      }
    }
  }
  candidate(item) {return this.serial(async()=>{
    if(!this.state.enabled || !eligible(item,this.api.ownId) || this.state.bypass.includes(item.id) || this.state.pending[item.id] || Object.keys(this.state.pending).length>=20) return;
    this.state.pending[item.id]={key:this.api.key(),phase:"created",keep:false};await this.save();await this.run(String(item.id));
  });}
  reconcile() {return this.serial(async()=>{for(const id of Object.keys(this.state.pending))await this.run(id);});}
  configure(enabled) {this.requestedEnabled=!!enabled;return this.serial(async()=>{this.state.enabled=!!enabled;await this.save();if(!enabled)for(const id of Object.keys(this.state.pending))await this.keep(id);return this.summary();});}
  summary() {return {enabled:this.state.enabled,pending:Object.keys(this.state.pending).length,notice:this.state.notice};}
  status() {return this.serial(async()=>{let connected=false;try{connected=(await this.native("ping")).state==="ready";}catch{}return {...this.summary(),connected};});}
  leave() {return this.configure(false);}
}
