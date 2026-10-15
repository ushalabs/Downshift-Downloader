import assert from 'node:assert/strict';
import {saveImage} from '../extension/image-capture.js';
import {BrowserTransfers,browserSource,NativeConnection} from '../extension/browser-transfer.js';
let checks=0;
const check=(value,message)=>{assert(value,message);checks++;};
function fixture(){
 const f={item:{id:7,state:'in_progress',paused:true,url:'blob:https://example.com/generated',filename:'',mime:'application/octet-stream',bytesReceived:17,totalBytes:17,danger:'safe'},log:[],nativeStatus:'Paused',record:{key:'a'.repeat(32),phase:'browser-preparing',transport:'browser'}};
 f.controller={filenameHints:new Map([[7,'report.rbxmx']]),save:async()=>{},finish:async(id,notice)=>{f.finished=notice;},native:async(command,values)=>{f.log.push(command);f.last=values;if(command==='browser-prepare')return {state:'prepared'};if(command==='commit'){check(values.browserTransport===true && !values.browserCancelled,'Browser transport does not falsely attest browser cancellation');return {state:'committed'};}return {downloadStatus:f.nativeStatus};}};
 f.bridge=new BrowserTransfers({ensureApp:async()=>({capabilities:{browserTransfer:true}}),key:()=> 'b'.repeat(32),item:async()=>f.item,route:async(id,name)=>{f.log.push('route');f.item.filename='C:/Downloads/'+name;return true;},pause:async()=>{f.log.push('pause');f.item.paused=true;},resume:async()=>{f.log.push('resume');f.item.paused=false;},cancel:async()=>{f.log.push('cancel');},removeFile:async()=>{f.log.push('remove');},erase:async()=>{f.log.push('erase');}});
 return f;
}
{
 const f=fixture();await f.bridge.run('7',f.record,f.item,f.controller);check(f.record.phase==='browser-streaming' && f.record.browserCommitSent,'Acceptance is recorded before browser streaming');check(f.log.indexOf('commit')<f.log.indexOf('route'),'Native acceptance precedes filename routing');check(!f.log.includes('resume') && !f.log.includes('erase'),'Destination selection holds the original response');
 f.nativeStatus='Downloading';await f.bridge.run('7',f.record,f.item,f.controller);check(f.log.includes('resume'),'Native Start enables the original browser response');
 f.nativeStatus='Paused';await f.bridge.run('7',f.record,f.item,f.controller);check(f.item.paused,'Native Pause controls browser transport');
 f.item.state='complete';f.item.fileSize=17;f.nativeStatus='Completed';await f.bridge.run('7',f.record,f.item,f.controller);check(f.log.indexOf('remove')<f.log.indexOf('erase') && f.finished==='Saved by Downshift.','Cleanup follows verified native completion');
}
{
 const f=fixture();f.item.danger='content';await assert.rejects(f.bridge.run('7',f.record,f.item,f.controller));check(f.log.length===0,'Browser security rejection is never bypassed');
}
{
 const f=fixture();f.record.phase='browser-streaming';f.item.state='interrupted';f.item.error='NETWORK_FAILED';f.item.canResume=false;f.nativeStatus='Failed';await f.bridge.run('7',f.record,f.item,f.controller);check(f.last.sourceError==='NETWORK_FAILED' && f.log.includes('cancel') && f.log.includes('erase'),'Nonrecoverable network errors reach the native row and cancel incomplete Chrome storage');
}
check(browserSource({url:'data:application/octet-stream;base64,'+'A'.repeat(100000),mime:'application/octet-stream'})==='data:application/octet-stream','Large inline data is not copied into persistent metadata');
{
 const f=fixture();f.record.phase='browser-streaming';f.item.state='complete';f.item.fileSize=17;f.nativeStatus='Cancelled';await f.bridge.run('7',f.record,f.item,f.controller);check(!f.log.includes('remove'),'Cancellation/history removal preserves the only fully received copy');
}
{
 let onMessage,onDisconnect;const sent=[];const port={onMessage:{addListener:f=>{onMessage=f;}},onDisconnect:{addListener:f=>{onDisconnect=f;}},postMessage:m=>{sent.push(m.number);queueMicrotask(()=>onMessage({ok:true,number:m.number}));},disconnect:()=>onDisconnect()};
 const connection=new NativeConnection(()=>port);const replies=await Promise.all([connection.request({number:1}),connection.request({number:2})]);clearTimeout(connection.idle);check(sent.join(',')==='1,2' && replies.map(r=>r.number).join(',')==='1,2','Persistent native connection preserves request/reply order');
}
console.log(`Browser bridge checks passed: ${checks}`);

{
 const f=fixture();f.record.phase='browser-streaming';f.record.routed=true;f.item.filename='';f.nativeStatus='Queued';await f.bridge.run('7',f.record,f.item,f.controller);check(f.log.includes('resume') && f.last.file==='','Accepted queued response resumes before Chrome finalizes its filename');
}
{
 let options;await saveImage({captureEnabled:async()=>true,ensureApp:async()=>{},download:async value=>{options=value;return 42;}},{srcUrl:'https://example.com/photo.webp'},{});check(options.saveAs===false && options.filename==='photo.webp','Image action avoids Chrome chooser and preserves format');
 await assert.rejects(saveImage({captureEnabled:async()=>false},{srcUrl:'https://example.com/photo.webp'},{}));
}
