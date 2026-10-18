// Observe manifest bodies already fetched by the player. No extra requests,
// credentials, decryption, player API replacement or persistent URL storage.
(() => {
  if(globalThis.__downshiftManifestProbe)return;globalThis.__downshiftManifestProbe=true;
  const seen=new Map();let active=0;
  addEventListener("message",event=>{if(event.source===window && (event.origin===location.origin||location.origin==="null") && event.data?.type==="downshift-manifest-replay")for(const [url,hint] of seen)postMessage({type:"downshift-manifest-observed",url,...hint},location.origin==="null"?"*":location.origin);});
  function report(url,text){
    if(typeof url!=="string" || url.length>32768 || !/^https?:\/\//i.test(url))return;
    const prefix=text.trimStart();const format=prefix.startsWith("#EXTM3U")?"hls":/^(?:<\?xml[^>]*>\s*)?<(?:[\w.-]+:)?MPD(?:\s|>)/i.test(prefix)?"dash":null;
    const master=format==="hls" && /#EXT-X-(?:STREAM-INF|I-FRAME-STREAM-INF|MEDIA):/.test(prefix);
    if(!format || (seen.get(url)?.format===format && seen.get(url)?.master===master))return;
    seen.set(url,{format,master});if(seen.size>32)seen.delete(seen.keys().next().value);
    postMessage({type:"downshift-manifest-observed",url,format,master},location.origin==="null"?"*":location.origin);
  }
  function inspect(response){
    if(active>=8 || !response.ok || !response.body || response.type==="opaque")return;
    const mime=response.headers.get("Content-Type")||"";
    if(/^(?:video\/|audio\/|image\/)/i.test(mime))return;
    const reader=response.clone().body.getReader();active++;
    const timer=setTimeout(()=>reader.cancel().catch(()=>{}),3000);
    (async()=>{let text="";while(text.length<256){const {done,value}=await reader.read();if(done)break;if(value)text+=new TextDecoder().decode(value.subarray(0,4096-text.length));}report(response.url,text);})().catch(()=>{}).finally(()=>{clearTimeout(timer);active--;reader.cancel().catch(()=>{});});
  }
  const fetchOriginal=globalThis.fetch;
  if(fetchOriginal)globalThis.fetch=function(...args){return Reflect.apply(fetchOriginal,this,args).then(response=>{try{inspect(response);}catch{}return response;});};
  const openOriginal=XMLHttpRequest.prototype.open;
  XMLHttpRequest.prototype.open=function(...args){
    this.addEventListener("load",()=>{try{
      const mime=this.getResponseHeader("Content-Type")||"";if(/^(?:video\/|audio\/|image\/)/i.test(mime))return;
      if(this.responseType===""||this.responseType==="text")report(this.responseURL,this.responseText.slice(0,4096));
      else if(this.responseType==="arraybuffer" && this.response?.byteLength<=1048576)report(this.responseURL,new TextDecoder().decode(new Uint8Array(this.response,0,Math.min(4096,this.response.byteLength))));
    }catch{}},{once:true});
    return Reflect.apply(openOriginal,this,args);
  };
})();
