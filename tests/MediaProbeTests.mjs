import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';
const messages=[],listeners={},requests=[];
class Xhr {
  open(){this.opened=true;}
  addEventListener(name,listener){this.listener=listener;}
  getResponseHeader(){return 'text/plain';}
}
const context=vm.createContext({Map,Reflect,TextDecoder,Uint8Array,XMLHttpRequest:Xhr,setTimeout,clearTimeout,
  location:{origin:'https://player.test'},postMessage:value=>messages.push(value),addEventListener:(name,listener)=>listeners[name]=listener,
  fetch:async url=>{requests.push(url);const response=new Response('#EXTM3U\n#EXT-X-ENDLIST\n',{headers:{'Content-Type':'text/plain'}});Object.defineProperty(response,'url',{value:url});return response;}});
context.window=context;
const script=fs.readFileSync(new URL('../extension/media-probe.js',import.meta.url),'utf8');vm.runInContext(script,context);
const response=await context.fetch('https://cdn.test/opaque.txt');assert.equal(await response.text(),'#EXTM3U\n#EXT-X-ENDLIST\n');
for(let n=0;n<20&&!messages.length;n++)await new Promise(resolve=>setTimeout(resolve,5));
assert.equal(requests.length,1,'Probe must not issue another request');assert.equal(messages.length,1);assert.equal(messages[0].format,'hls');
const wrapper=context.fetch;vm.runInContext(script,context);assert.equal(context.fetch,wrapper,'Repeated script must not wrap twice');
vm.runInContext("const xhr=new XMLHttpRequest();xhr.responseType='text';xhr.responseURL='https://cdn.test/manifest';xhr.responseText='<?xml version=\"1.0\"?><MPD type=\"static\">';xhr.open('GET',xhr.responseURL);xhr.listener();",context);
assert.equal(messages.at(-1).format,'dash');
console.log('Media probe checks passed: fetch body preserved, no extra fetch, HLS/DASH generic MIME, duplicate injection guarded.');
{
 const sent=[],callbacks=[],events={};let response={playabilityStatus:{status:'OK'},videoDetails:{videoId:'FQmqRVK3kHg',lengthSeconds:'30'},streamingData:{adaptiveFormats:[{itag:137,mimeType:'video/mp4; codecs="avc1"',height:1080,contentLength:'100',signatureCipher:'url='+encodeURIComponent('https://rr1.googlevideo.com/videoplayback?id=only-this-resource')}]}};
 const calls=[];
 const page=vm.createContext({Map,Set,Reflect,TextDecoder,Uint8Array,URL,URLSearchParams,Date,Response,AbortController,XMLHttpRequest:Xhr,setTimeout,clearTimeout,setInterval:callback=>callbacks.push(callback),
  ytcfg:{get:key=>key==='INNERTUBE_CONTEXT'?{client:{clientVersion:'test-version',visitorData:'fixture-visitor'}}:key==='STS'?123:undefined},
  fetch:async(url,options)=>{calls.push({url,options});return new Response(JSON.stringify({playabilityStatus:{status:'OK'},videoDetails:{videoId:'FQmqRVK3kHg',lengthSeconds:'30'},streamingData:{hlsManifestUrl:'https://manifest.googlevideo.com/api/manifest/hls_variant/id/test/index.m3u8'}}));},
  location:{hostname:'www.youtube.com',origin:'https://www.youtube.com',href:'https://www.youtube.com/watch?v=FQmqRVK3kHg'},
  document:{getElementById:()=>({getPlayerResponse:()=>response}),querySelector:()=>({mediaKeys:null})},
  postMessage:value=>sent.push(value),addEventListener:(name,listener)=>events[name]=listener});page.window=page;page.top=page;
 vm.runInContext(fs.readFileSync(new URL('../extension/youtube-player.js',import.meta.url),'utf8'),page);callbacks[0]();assert.equal(sent[0].type,'downshift-youtube-observed');assert.equal(sent[0].video.formats[0].resourceId,'only-this-resource');assert(!JSON.stringify(sent).includes('signatureCipher'));
 callbacks[0]();assert.equal(sent.length,1,'Unchanged descriptors are throttled');
 response.videoDetails.videoId='HyfC9nZvIJM';callbacks[0]();assert.equal(sent.length,1,'Stale SPA player response is rejected');
 response.videoDetails.videoId='FQmqRVK3kHg';response.videoDetails.isLiveContent=true;callbacks[0]();assert.equal(sent.length,1,'Live data is not offered as a complete recording');
 response.videoDetails.isLiveContent=false;
 page.receiveMessage=events.message;vm.runInContext("receiveMessage({source:window,origin:'https://www.youtube.com',data:{type:'downshift-youtube-prepare',nonce:'00000000-0000-0000-0000-000000000000'}})",page);
 for(let n=0;n<20&&!sent.some(x=>x.type==='downshift-youtube-prepared');n++)await new Promise(resolve=>setTimeout(resolve,5));
 assert.equal(calls.length,1);assert(calls[0].url.startsWith('/youtubei/v1/player?'));assert.equal(calls[0].options.credentials,'include');assert.equal(calls[0].options.mode,'same-origin');assert.equal(calls[0].options.redirect,'error');assert(!calls[0].options.headers.Cookie&&!calls[0].options.headers.Authorization);
 assert(sent.at(-1).video.manifest.includes('hls_variant'));assert(!JSON.stringify(sent).includes('fixture-visitor'),'Visitor/session context never crosses the page bridge');
 // An actual clicked home-feed player has no ?v= in its document URL.
 page.location.href='https://www.youtube.com/';
 const clicked={mediaKeys:null,duration:30,closest:()=>({getVideoData:()=>({video_id:'HyfC9nZvIJM'}),getPlayerResponse:()=>JSON.stringify({playabilityStatus:{status:'OK'},videoDetails:{videoId:'HyfC9nZvIJM',lengthSeconds:'30'},streamingData:{adaptiveFormats:[{itag:137,mimeType:'video/mp4',height:1080,contentLength:'100',url:'https://rr1.googlevideo.com/videoplayback?itag=137'},{itag:140,mimeType:'audio/mp4',contentLength:'30',url:'https://rr1.googlevideo.com/videoplayback?itag=140'}]}})})};
 page.document.querySelector=()=>clicked;
 vm.runInContext("receiveMessage({source:window,origin:'https://www.youtube.com',data:{type:'downshift-youtube-prepare',nonce:'00000000-0000-0000-0000-000000000001'}})",page);
 for(let n=0;n<20&&!sent.some(x=>x.nonce==='00000000-0000-0000-0000-000000000001');n++)await new Promise(resolve=>setTimeout(resolve,5));
 assert.equal(sent.at(-1).video.id,'HyfC9nZvIJM');assert.equal(calls.length,1,'Feed standard tracks require no extra request');
 console.log('YouTube observer checks passed: click-only bounded same-origin HLS preparation; serialized home-feed player resolution; no session/cipher export; stale/live responses rejected.');
}
