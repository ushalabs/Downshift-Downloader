import {HandoffController} from "./handoff.js";
const controller=new HandoffController({
  ownId:chrome.runtime.id,
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
  if(message.type==="status")task=controller.status();
  else if(message.type==="configure")task=controller.configure(message.enabled);
  else if(message.type==="leave")task=controller.leave();
  else if(message.type==="open")task=controller.serial(()=>controller.native("open"));
  else if(message.type==="reconcile")task=controller.reconcile().then(()=>controller.status());
  else return false;
  task.then(value=>reply({ok:true,...value})).catch(()=>reply({ok:false,error:"Downshift is unavailable. Open the app and try again."}));return true;
});
quiet(controller.reconcile());
