export const firefox=typeof globalThis.browser?.runtime?.getBrowserInfo==='function';
export const api=firefox ? globalThis.browser:globalThis.chrome;

export function downloadItem(item){
  if(!item)return item;
  return {...item,...(item.startTime instanceof Date ? {startTime:item.startTime.toISOString()}:{}),
    ...(item.endTime instanceof Date ? {endTime:item.endTime.toISOString()}:{})};
}

async function downloadIdentity(item){
  const address=String(item.url || '');
  const source=(address.startsWith('data:') ? address.slice(0,128):address.slice(0,32768))+'\0'+String(item.filename || '');
  const digest=await crypto.subtle.digest('SHA-256',new TextEncoder().encode(source));
  return Array.from(new Uint8Array(digest),byte=>byte.toString(16).padStart(2,'0')).join('');
}

// Firefox changes startTime when a paused download starts again. Keep its first
// identity while it is live, and retain only hashed identities for recovery.
export class FirefoxDownloads {
  constructor(storage,downloads,canPause=()=>true){
    this.storage=storage;
    this.downloads=downloads;
    this.canPause=canPause;
    this.live=new Map();
    this.known=new Map();
    this.writes=Promise.resolve();
    this.ready=storage.get('firefoxDownloadIdentities').then(saved=>{
      for(const entry of Array.isArray(saved.firefoxDownloadIdentities) ? saved.firefoxDownloadIdentities:[]){
        if(/^[a-f0-9]{64}$/.test(entry.identity) && Number.isFinite(Date.parse(entry.startTime)) && Number.isFinite(Date.parse(entry.observed)))this.known.set(entry.identity,entry);
      }
    });
  }
  async normalize(value,created=false){
    await this.ready;
    const item=downloadItem(value);
    if(!item)return item;
    // Firefox implements Pause as an interrupted download with retained data.
    // It is distinct from Cancel, which discards the partial and clears paused.
    if(item.paused && item.state==='interrupted'){
      item.state='in_progress';
      delete item.error;
    }
    const identity=await downloadIdentity(item);
    const previous=created ? undefined:this.live.get(item.id);
    const known=this.known.get(identity);
    const startTime=previous?.startTime || (known?.observed===item.startTime ? known.startTime:item.startTime);
    this.live.set(item.id,{identity,startTime});
    const observed=known && Date.parse(known.observed)>Date.parse(item.startTime) ? known.observed:item.startTime;
    const active=item.state==='in_progress' || item.canResume===true;
    if(!known || known.startTime!==startTime || known.observed!==observed || known.active!==active){
      this.known.delete(identity);
      this.known.set(identity,{identity,startTime,observed,active});
      const values=Array.from(this.known.values());
      const retained=values.filter(entry=>entry.active).concat(values.filter(entry=>!entry.active).slice(-1000));
      this.known=new Map(retained.map(entry=>[entry.identity,entry]));
      this.writes=this.writes.catch(()=>{}).then(()=>this.storage.set({firefoxDownloadIdentities:retained}));
      await this.writes;
    }
    return {...item,startTime};
  }
  async search(query){
    return Promise.all((await this.downloads.search(query)).map(item=>this.normalize(item)));
  }
  async pause(id){
    const item=(await this.downloads.search({id}))[0];
    // Pausing before Firefox owns a resumable prefix can destroy a POST or
    // signed response. Retain that original browser response until it settles.
    if(item?.state==='in_progress' && item.bytesReceived>0 && this.canPause(item))await this.downloads.pause(id);
  }
  async restore(state){
    if(!state || !Object.keys(state.pending || {}).length)return state;
    const required=new Set(Object.values(state.pending).map(record=>record.firefoxIdentity));
    const items=[];
    for(const item of await this.downloads.search({})){
      if(required.has(await downloadIdentity(item)))items.push(await this.normalize(item));
    }
    const pending={};
    for(const [id,record] of Object.entries(state.pending || {})){
      const matches=items.filter(item=>item.startTime===record.startTime && this.live.get(item.id)?.identity===record.firefoxIdentity);
      pending[matches.length===1 ? String(matches[0].id):id]=record;
    }
    return {...state,pending};
  }
  async save(state){
    for(const [id,record] of Object.entries(state.pending || {})){
      const identity=this.live.get(Number(id))?.identity;
      if(identity)record.firefoxIdentity=identity;
    }
    await this.storage.set({handoffState:state});
  }
}
