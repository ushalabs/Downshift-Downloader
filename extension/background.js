import {HandoffController,safeFilename} from "./handoff.js";
import {MediaSources,publicUrl} from "./media.js";
import {BrowserTransfers,NativeConnection} from './browser-transfer.js';
const mediaSources = new MediaSources();
const connection=new NativeConnection(()=>chrome.runtime.connectNative('org.ushalabs.downshift'));
const sourceTags=new Map();
chrome.webRequest.onBeforeRequest.addListener(details=>{if(details.method!=='GET')sourceTags.delete(details.url);},{urls:['http://*/*','https://*/*']});
async function ensureApp() {
  const request = command=>connection.request({version:1,command});
  const opened=await request("open");if(!opened?.ok)throw Error("Could not open Downshift");
  for(let attempt=0;attempt<8;attempt++) {
    let answer;try{answer=await request("ping");}catch(error){if(opened.state!=="opening")throw error;}
    if(answer?.ok){if(answer.appVersion!==chrome.runtime.getManifest().version)throw Error("Close the old Downshift window and open the updated Desktop shortcut, then retry.");return answer;}
    if(answer?.error && opened.state!=="opening")throw Error(answer.error);
    await new Promise(resolve=>setTimeout(resolve,400));
  }
  throw Error("Downshift is still starting");
}
chrome.webRequest.onHeadersReceived.addListener(details=>{
  mediaSources.observe(details);
  if(details.method==='GET' && details.statusCode===200){const tag=details.responseHeaders?.find(h=>h.name.toLowerCase()==='etag')?.value;if(tag && /^"[^\r\n]{1,500}"$/.test(tag)){sourceTags.delete(details.url);sourceTags.set(details.url,{tag,at:Date.now()});if(sourceTags.size>1000)sourceTags.delete(sourceTags.keys().next().value);}}
}, {urls:["http://*/*","https://*/*"]}, ["responseHeaders"]);
chrome.tabs.onRemoved.addListener(id=>mediaSources.tabs.delete(id));
const controller=new HandoffController({
  ownId:chrome.runtime.id,
  captureEnabled:async()=>(await chrome.storage.local.get("handoffState")).handoffState?.enabled!==false,
  ensureApp,
  load:async()=>(await chrome.storage.local.get("handoffState")).handoffState,
  save:async state=>chrome.storage.local.set({handoffState:state}),
  key:()=>crypto.randomUUID().replaceAll("-",""),
  native:request=>connection.request(request),
  sourceTag:item=>{const source=sourceTags.get(item.finalUrl || item.url);return source && Date.now()-source.at<60000 ? source.tag:'';},
  browserCapture:(id,record,item,owner)=>browserTransfers.run(id,record,item,owner),
  item:async id=>(await chrome.downloads.search({id}))[0],
  active:()=>chrome.downloads.search({state:"in_progress"}),
  scheduleRetry:when=>{
    // A short timer handles normal startup/network delays; the alarm survives a
    // suspended MV3 worker. Neither path ever resumes Chrome.
    setTimeout(()=>quiet(controller.reconcile()),Math.max(0,when-Date.now()));
    chrome.alarms.create("downshift-retry",{when});
  },
  pause:id=>chrome.downloads.pause(id),resume:id=>chrome.downloads.resume(id),cancel:id=>chrome.downloads.cancel(id),
  erase:id=>chrome.downloads.erase({id}),
  restart:item=>chrome.downloads.download({url:item.finalUrl || item.url,filename:(item.filename || "download.bin").split(/[\\/]/).pop(),conflictAction:"uniquify",saveAs:false})
});
const quiet=promise=>promise.catch(()=>{});
const filenameHolds=new Map();
const filenameRoutes=new Map();
function applyFilenameRoute(id){const suggest=filenameHolds.get(id),route=filenameRoutes.get(id);if(suggest && route){filenameHolds.delete(id);filenameRoutes.delete(id);suggest({filename:route.name,conflictAction:'uniquify'});route.resolve(true);}}
const browserTransfers=new BrowserTransfers({ensureApp,key:()=>crypto.randomUUID().replaceAll('-',''),
  item:async id=>(await chrome.downloads.search({id}))[0],pause:id=>chrome.downloads.pause(id),resume:id=>chrome.downloads.resume(id),cancel:id=>chrome.downloads.cancel(id),erase:id=>chrome.downloads.erase({id}),
  removeFile:async id=>{try{await chrome.downloads.removeFile(id);}catch(error){const item=(await chrome.downloads.search({id}))[0];if(item?.exists!==false)throw error;}},
  route:(id,name)=>new Promise(resolve=>{filenameRoutes.set(id,{name,resolve});applyFilenameRoute(id);setTimeout(()=>{if(filenameRoutes.get(id)?.resolve===resolve){filenameRoutes.delete(id);resolve(false);}},1500);})
});
async function releaseFilenameHolds(){
  const enabled=(await chrome.storage.local.get("handoffState")).handoffState?.enabled!==false;
  for(const [id,suggest] of filenameHolds){
    const item=(await chrome.downloads.search({id}))[0];
    // Storage and download events can overlap while search is awaiting. Only
    // the first caller may release this exact callback.
    if(filenameHolds.get(id)!==suggest)continue;
    if(!enabled || !item || item.state!=="in_progress"){filenameHolds.delete(id);suggest();}
  }
}
chrome.downloads.onCreated.addListener(item=>quiet(controller.candidate(item,'created')));
// Hold filename finalization until the capture decision has settled. Chrome
// cannot complete even a tiny attachment while this callback is outstanding.
// Always release it exactly once; failures remain paused, not silently resumed.
chrome.downloads.onDeterminingFilename.addListener((item,suggest)=>{
  filenameHolds.set(item.id,suggest);
  applyFilenameRoute(item.id);
  quiet(controller.candidate(item,'filename').finally(()=>releaseFilenameHolds()));return true;
});
chrome.downloads.onChanged.addListener(change=>{
  if(change.state || change.paused || change.finalUrl || change.url || change.mime || change.filename || change.totalBytes || change.danger)
    quiet(chrome.downloads.search({id:change.id}).then(async items=>{
      if(items[0] && ['in_progress','complete'].includes(items[0].state))await controller.candidate(items[0]);
      else await controller.serial(()=>controller.run(String(change.id)));
      await releaseFilenameHolds();
    }));
});
chrome.storage.onChanged.addListener((changes,area)=>{if(area==="local" && changes.handoffState)quiet(releaseFilenameHolds());});
let reconciling=false;
setInterval(()=>{if(reconciling || !Object.values(controller.state?.pending || {}).some(r=>r.transport==='browser'))return;reconciling=true;controller.reconcile().catch(()=>{}).finally(()=>{reconciling=false;});},500);
async function captureUi(){const enabled=(await chrome.storage.local.get('handoffState')).handoffState?.enabled!==false;try{await chrome.downloads.setUiOptions({enabled:!enabled});}catch{}}
chrome.storage.onChanged.addListener((changes,area)=>{if(area==='local' && changes.handoffState)quiet(captureUi());});quiet(captureUi());
chrome.alarms.onAlarm.addListener(alarm=>{if(alarm.name==="downshift-reconcile" || alarm.name==="downshift-retry")quiet(controller.reconcile());});
chrome.runtime.onStartup.addListener(()=>quiet(controller.reconcile()));
chrome.runtime.onInstalled.addListener(()=>{chrome.alarms.create("downshift-reconcile",{periodInMinutes:.5});quiet(controller.reconcile());});
chrome.runtime.onMessage.addListener((message,sender,reply)=>{
  if(sender.id!==chrome.runtime.id || !message || typeof message.type!=="string")return false;
  let task;
  if(message.type==="media") {
    task = (async()=>{
      let current=message;
      // Existing content scripts can outlive an extension update/navigation.
      // For a top-level YouTube player also consult Chrome's current tab URL.
      if(sender.frameId===0 && sender.tab?.id!==undefined){
        try{const tab=await chrome.tabs.get(sender.tab.id);if(/(^|\.)youtube\.com$/.test(new URL(tab.url).hostname))current={...message,page:tab.url};}catch{}
      }
      const url=mediaSources.resolve(current,sender);
      if(!url)throw Error("This player has no supported public video address. Refresh its video page and retry.");
      const opening = await controller.native("open");
      for(let attempt=0;attempt<8;attempt++) {
        try { return await controller.native("media",{url,title:String(message.title || "Video").slice(0,200)}); }
        catch(error) { if(opening.state!=="opening" || attempt===7)throw error;await new Promise(resolve=>setTimeout(resolve,400)); }
      }
    })();
  }
  else if(message.type==="link") {
    const url=publicUrl(message.url);
    if(!url || !publicUrl(sender.url) || sender.tab?.incognito){reply({ok:false,error:"This link cannot be captured."});return false;}
    let pathName=new URL(url).pathname.split('/').pop();try{pathName=decodeURIComponent(pathName);}catch{}
    const name=safeFilename(message.filename || pathName);
    task=controller.direct(url,name || "download.bin",!message.filename);
  }
  else if(message.type==="status")task=controller.status();
  else if(message.type==="configure")task=controller.configure(message.enabled).then(async state=>{if(state.enabled)await controller.reconcile();return state;});
  else if(message.type==="leave")task=controller.leave();
  else if(message.type==="open")task=controller.serial(()=>controller.native("open"));
  else if(message.type==="reconcile")task=controller.reconcile().then(()=>controller.status());
  else if(message.type==="retry")task=controller.retry().then(()=>controller.status());
  else return false;
  task.then(value=>reply({ok:true,...value,captureProtocol:4})).catch(error=>reply({ok:false,captureProtocol:4,error:String(error?.message || "Downshift is unavailable. Open the app and try again.").slice(0,240)}));return true;
});
quiet(controller.reconcile());
