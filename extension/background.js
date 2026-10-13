import {HandoffController} from "./handoff.js";
import {MediaSources,publicUrl} from "./media.js";
const mediaSources = new MediaSources();
async function ensureApp() {
  const request = command=>chrome.runtime.sendNativeMessage("org.ushalabs.downshift",{version:1,command});
  const opened=await request("open");if(!opened?.ok)throw Error("Could not open Downshift");
  for(let attempt=0;attempt<8;attempt++) {try{if((await request("ping")).ok)return;}catch{}await new Promise(resolve=>setTimeout(resolve,400));}
  throw Error("Downshift is still starting");
}
chrome.webRequest.onHeadersReceived.addListener(details=>mediaSources.observe(details), {urls:["http://*/*","https://*/*"]}, ["responseHeaders"]);
chrome.tabs.onRemoved.addListener(id=>mediaSources.tabs.delete(id));
const controller=new HandoffController({
  ownId:chrome.runtime.id,
  captureEnabled:async()=>(await chrome.storage.local.get("handoffState")).handoffState?.enabled!==false,
  ensureApp,
  load:async()=>(await chrome.storage.local.get("handoffState")).handoffState,
  save:async state=>chrome.storage.local.set({handoffState:state}),
  key:()=>crypto.randomUUID().replaceAll("-",""),
  native:async request=>{
    let timeout;
    try{return await Promise.race([chrome.runtime.sendNativeMessage("org.ushalabs.downshift",request),new Promise((_,reject)=>{timeout=setTimeout(()=>reject(new Error("Native timeout")),16000);})]);}
    finally{clearTimeout(timeout);}
  },
  item:async id=>(await chrome.downloads.search({id}))[0],
  pause:id=>chrome.downloads.pause(id),resume:id=>chrome.downloads.resume(id),cancel:id=>chrome.downloads.cancel(id),
  erase:id=>chrome.downloads.erase({id}),
  restart:item=>chrome.downloads.download({url:item.finalUrl || item.url,filename:(item.filename || "download.bin").split(/[\\/]/).pop(),conflictAction:"uniquify",saveAs:false})
});
const quiet=promise=>promise.catch(()=>{});
chrome.downloads.onCreated.addListener(item=>quiet(controller.candidate(item)));
chrome.downloads.onChanged.addListener(change=>{
  if(change.state || change.paused)quiet(controller.reconcile());
  if(change.mime || change.filename || change.totalBytes || change.danger)quiet(chrome.downloads.search({id:change.id}).then(items=>items[0] && controller.candidate(items[0])));
});
chrome.alarms.onAlarm.addListener(alarm=>{if(alarm.name==="downshift-reconcile")quiet(controller.reconcile());});
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
    const name=String(message.filename || pathName || "download.bin").split(/[\\/]/).pop().replace(/[<>:"|?*\x00-\x1f]/g,'_').slice(0,150);
    task=controller.direct(url,name || "download.bin");
  }
  else if(message.type==="status")task=controller.status();
  else if(message.type==="configure")task=controller.configure(message.enabled);
  else if(message.type==="leave")task=controller.leave();
  else if(message.type==="open")task=controller.serial(()=>controller.native("open"));
  else if(message.type==="reconcile")task=controller.reconcile().then(()=>controller.status());
  else if(message.type==="retry")task=controller.retry().then(()=>controller.status());
  else return false;
  task.then(value=>reply({ok:true,...value})).catch(error=>reply({ok:false,error:String(error?.message || "Downshift is unavailable. Open the app and try again.").slice(0,240)}));return true;
});
quiet(controller.reconcile());
