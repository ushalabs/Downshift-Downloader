import {safeFilename} from './handoff.js';

// Keep the original browser request/response for generated, authenticated and
// POST downloads. Only metadata crosses Native Messaging; the app streams the
// owned transport file into its exclusive, verified destination.
export class NativeConnection {
  constructor(connect){this.connect=connect;this.port=null;this.pending=null;this.chain=Promise.resolve();}
  request(message){const task=this.chain.then(()=>this.send(message));this.chain=task.catch(()=>{});return task;}
  send(message){
    if(!this.port){
      const port=this.port=this.connect();
      port.onMessage.addListener(answer=>{if(this.port===port && this.pending){const pending=this.pending;this.pending=null;clearTimeout(pending.timer);pending.resolve(answer);}});
      port.onDisconnect.addListener(()=>{void globalThis.chrome?.runtime?.lastError;if(this.port===port){this.port=null;if(this.pending){const pending=this.pending;this.pending=null;clearTimeout(pending.timer);pending.reject(Error('Reconnecting Downshift…'));}}});
    }
    clearTimeout(this.idle);this.idle=setTimeout(()=>{if(!this.pending && this.port){const port=this.port;this.port=null;port.disconnect();}},60000);
    return new Promise((resolve,reject)=>{
      const timer=setTimeout(()=>{this.pending=null;const port=this.port;this.port=null;port?.disconnect();reject(Error('Downshift is taking longer to respond. Reconnecting…'));},16000);
      this.pending={resolve,reject,timer};try{this.port.postMessage(message);}catch(error){clearTimeout(timer);this.pending=null;reject(error);}
    });
  }
}
export function browserSource(item){
  const value=String(item.finalUrl || item.url || 'browser:download');
  // Never copy a potentially huge data URI into messages, history or logs.
  return value.startsWith('data:') ? 'data:'+(String(item.mime || 'application/octet-stream').slice(0,120)):value.slice(0,32768);
}
export class BrowserTransfers {
  constructor(api){this.api=api;}
  async run(id,record,item,controller){
    if(!item){await controller.finish(id,'Download removed.');return;}
    if(item.danger && !['safe','accepted','allowlistedByPolicy','deepScannedSafe'].includes(item.danger))throw Error('Waiting for Chrome’s security check.');
    if(record.phase!=='browser-streaming'){
      const app=await this.api.ensureApp();if(!app.capabilities?.browserTransfer)throw Error('Open the updated Downshift app to receive this download.');
      const filename=safeFilename(item.filename || controller.filenameHints.get(Number(id)) || 'download.bin');
      const receipt=await controller.native('browser-prepare',{key:record.key,url:browserSource(item),filename,total:String(item.totalBytes ?? -1),mime:String(item.mime || '').slice(0,120)});
      if(receipt.state==='aborted'){record.key=this.api.key();record.phase='browser-preparing';await controller.save();return;}
      record.browserCommitSent=true;await controller.save();
      const committed=await controller.native('commit',{key:record.key,browserTransport:true,prompt:true});
      if(committed.state!=='committed')throw Error('Waiting for Downshift to confirm this transfer.');
      record.phase='browser-streaming';record.keep=false;if(controller.state)controller.state.notice='Download opened in Downshift.';await controller.save();
      const suffix=filename.match(/\.[^.]{1,20}$/)?.[0] || '.bin';
      record.routed=await this.api.route(Number(id),`Downshift Transfers/${record.key}/payload${suffix}`);await controller.save();
      item=await this.api.item(Number(id));if(!item)return;
    }
    const routed= !record.routed || String(item.filename || '').includes(record.key) || item.state==='complete';
    const complete=item.state==='complete';
    const total=complete ? (item.fileSize>=0 ? item.fileSize:item.bytesReceived) : (item.totalBytes ?? -1);
    const sourceError=item.state==='interrupted' && (!item.canResume || (record.retries || 0)>=5) ? String(item.error || 'NETWORK_FAILED').replace(/[^A-Z_0-9]/g,'').slice(0,80):'';
    const result=await controller.native('browser-update',{key:record.key,file:routed ? (item.filename || ''):'',complete:complete && routed,total:String(total),received:String(item.bytesReceived || 0),...(sourceError ? {sourceError}:{})});
    if(result.downloadStatus==='Completed'){
      // Native completion includes source/copy SHA-256, size, flush and atomic
      // rename. Only now remove this exact Chrome transport and its history.
      if(item.state!=='complete')throw Error('Waiting for the received file to finish.');
      await this.api.removeFile(Number(id));await this.api.erase(Number(id));await controller.finish(id,'Saved by Downshift.');return;
    }
    if(['Cancelled','Removed','Failed'].includes(result.downloadStatus)){
      if(item.state==='in_progress' || item.state==='interrupted')try{await this.api.cancel(Number(id));}catch(error){if(item.state==='in_progress')throw error;}
      // Incomplete Chrome transports are cancelled so Chrome removes its partial.
      // History removal/cancellation is not permission to delete the only full
      // received copy. Keep it when native completion has not been verified.
      await this.api.erase(Number(id));await controller.finish(id,'Download cancelled in Downshift.');return;
    }
    if(result.downloadStatus==='Downloading' || (result.downloadStatus==='Queued' && (!routed || !item.filename))){
      if(item.state==='in_progress' && item.paused)await this.api.resume(Number(id));
      else if(item.state==='interrupted' && item.canResume && (record.retries || 0)<5){record.retries=(record.retries || 0)+1;await controller.save();await this.api.resume(Number(id));}
    }else if(item.state==='in_progress' && !item.paused)await this.api.pause(Number(id));
  }
}
