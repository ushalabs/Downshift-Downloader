import {youtubeDescriptor,youtubeStreamUrl} from './youtube-streams.js';
// Media URLs live only in this worker's bounded memory, never in persistent storage.
export function publicUrl(value) {
  if(typeof value!=="string" || value.length>32768 || /[\x00-\x1f\x7f]/.test(value))return null;
  try { const url = new URL(value); return /^https?:$/.test(url.protocol) && !url.username && !url.password && value.length <= 32768 ? url.href : null; }
  catch { return null; }
}
export function youtubePage(value) {
  const address = publicUrl(value); if (!address) return null;
  const url = new URL(address);
  if (url.hostname === "youtu.be") {
    const id=url.pathname.split('/')[1];
    return /^[\w-]{11}$/.test(id || '') ? `https://www.youtube.com/watch?v=${id}` : null;
  }
  if (!/(^|\.)youtube\.com$/.test(url.hostname)) return null;
  const match = url.pathname.match(/^\/(?:embed|shorts|live)\/([\w-]{11})(?:\/|$)/);
  const id = url.searchParams.get("v") || match?.[1];
  return id && /^[\w-]{11}$/.test(id) ? `https://www.youtube.com/watch?v=${id}` : null;
}
export class MediaSources {
  constructor(now = () => Date.now()) { this.tabs = new Map(); this.requests = new Map(); this.youtube = new Map(); this.youtubeMedia = new Map(); this.now = now; }
  request(details) {
    if(details.tabId<0 || details.method!=="GET")return;
    const headers={};
    for(const h of details.requestHeaders || []){
      const name={referer:"referer",origin:"origin","user-agent":"userAgent"}[h.name.toLowerCase()];
      if(name && typeof h.value==="string" && h.value.length<=2048 && !/[\x00-\x1f\x7f]/.test(h.value))headers[name]=h.value;
    }
    this.requests.set(details.requestId,{url:publicUrl(details.url),document:details.documentId||"",headers,tab:details.tabId,frame:details.frameId,at:this.now()});
    if(this.requests.size>1024)this.requests.delete(this.requests.keys().next().value);
  }
  forget(tab) { this.tabs.delete(tab);this.youtube.delete(tab);this.youtubeMedia.delete(tab);for(const [id,value] of this.requests)if(value.tab===tab)this.requests.delete(id); }
  observeYouTube(message,sender) {
    const page=youtubePage(message.page),original=publicUrl(sender.url);
    if(!page || !original || new URL(original).origin!==new URL(message.page).origin || sender.frameId!==0 || sender.tab?.id===undefined || sender.tab.incognito)return;
    const video=message.video;
    if(!video || video.id!==new URL(page).searchParams.get('v') || !Array.isArray(video.formats) || video.formats.length>64 || JSON.stringify(video).length>48000)return;
    this.youtube.set(sender.tab.id,{page,document:sender.documentId||'',video,at:this.now()});
    if(this.youtube.size>32)this.youtube.delete(this.youtube.keys().next().value);
  }
  remember(tab,source) {
    const list=(this.tabs.get(tab)||[]).filter(x=>this.now()-x.at<600000);
    const previous=list.find(x=>x.url===source.url && x.frame===source.frame && x.document===source.document);
    if(previous){Object.assign(previous,source,{first:previous.first});}
    else list.push({...source,first:this.now()});
    this.tabs.delete(tab);this.tabs.set(tab,list.slice(-32));
    if(this.tabs.size>128)this.forget(this.tabs.keys().next().value);
  }
  probe(message,sender) {
    const original=publicUrl(sender.url)||publicUrl(sender.origin),current=publicUrl(message.page),url=publicUrl(message.url);
    const page=current||original;if(current && original && new URL(current).origin!==new URL(original).origin)return;
    if(!page || !url || sender.tab?.id===undefined || sender.tab.incognito || !["hls","dash"].includes(message.format))return;
    const previous=(this.tabs.get(sender.tab.id)||[]).find(x=>x.url===url && x.frame===sender.frameId && x.document===(sender.documentId||""));
    const request=[...this.requests.values()].reverse().find(x=>x.url===url && x.tab===sender.tab.id && x.frame===sender.frameId && (!x.document||!sender.documentId||x.document===sender.documentId) && this.now()-x.at<600000);
    this.remember(sender.tab.id,{...previous,url,frame:sender.frameId,document:sender.documentId||"",page,pageExact:true,kind:message.format,master:message.master===true||previous?.master===true,headers:request?.headers||previous?.headers||{},at:this.now()});
  }
  observe(details) {
    if (details.tabId < 0 || details.statusCode < 200 || details.statusCode >= 400) return;
    const url = publicUrl(details.url); if (!url || /\.(?:m4s|ts)(?:[?#]|$)/i.test(url)) return;
    const mime = details.responseHeaders?.find(h => h.name.toLowerCase() === "content-type")?.value || "";
    const kind=/mpegurl/i.test(mime)||/\.m3u8(?:[?#]|$)/i.test(url)?"hls":/dash\+xml/i.test(mime)||/\.mpd(?:[?#]|$)/i.test(url)?"dash":"video";
    const previous=(this.tabs.get(details.tabId)||[]).find(x=>x.url===url && x.frame===details.frameId && x.document===(details.documentId||""));
    if (!previous && !/^(?:video\/|application\/(?:vnd\.apple\.mpegurl|x-mpegurl|dash\+xml))/i.test(mime) && !/\.(?:mp4|webm|mov|m3u8|mpd)(?:[?#]|$)/i.test(url)) return;
    // YouTube's playback requests may be partial audio/video fragments. Resolve its page instead.
    if (/(^|\.)googlevideo\.com$/.test(new URL(url).hostname)) {
      const stream=youtubeStreamUrl(url);
      if(stream && /^(?:audio|video)\/(?:mp4|webm)(?:;|$)/i.test(mime)){
        const list=(this.youtubeMedia.get(details.tabId)||[]).filter(x=>this.now()-x.at<120000 && x.url!==stream);
        list.push({url:stream,frame:details.frameId,document:details.documentId||'',at:this.now()});this.youtubeMedia.set(details.tabId,list.slice(-24));
        if(this.youtubeMedia.size>32)this.youtubeMedia.delete(this.youtubeMedia.keys().next().value);
      }
      return;
    }
    const request=this.requests.get(details.requestId);this.requests.delete(details.requestId);
    this.remember(details.tabId,{url,frame:details.frameId,document:details.documentId||"",page:previous?.pageExact?previous.page:publicUrl(details.documentUrl)||publicUrl(details.initiator)||"",pageExact:previous?.pageExact||!!publicUrl(details.documentUrl),kind:previous?.kind||kind,master:previous?.master===true,headers:request?.headers||previous?.headers||{},at:this.now()});
  }
  resolve(message, sender) {
    const original = publicUrl(sender.url) || publicUrl(sender.origin) || publicUrl(sender.tab?.url);
    if (!original || sender.tab?.incognito) return null;
    // Chrome's sender.url can retain the document's initial URL after pushState.
    // Accept the content script's current location only within that same origin.
    const current = publicUrl(message.page);
    if (current && new URL(current).origin !== new URL(original).origin) return null;
    const page = current || original;
    if (/(^|\.)youtube\.com$/.test(new URL(page).hostname) || new URL(page).hostname === "youtu.be") {
      const player = youtubePage(message.videoPage);
      return player || youtubePage(page);
    }
    const direct = publicUrl(message.source); if (direct) return direct;
    const candidates = this.candidates(sender,page);
    // Adaptive players request a master and one or more variants. That is one
    // player, not an unsupported page. Prefer its earliest manifest, not a segment.
    const manifests=candidates.filter(x=>x.kind==="hls"||x.kind==="dash").sort((a,b)=>Number(b.master)-Number(a.master)||a.first-b.first);
    if(manifests.length && message.playerCount===1)return manifests[0].url;
    return candidates.length === 1 ? candidates[0].url : page;
  }
  candidates(sender,page) { return (this.tabs.get(sender.tab?.id)||[]).filter(x=>x.frame===sender.frameId && this.now()-x.at<600000 && (!sender.documentId || x.document===sender.documentId) && (!x.page || (new URL(x.page).origin===new URL(page).origin && (!x.pageExact || x.page.split('#')[0]===page.split('#')[0])))); }
  resolveRequest(message,sender) {
    if(message.protected)return null;
    const url=this.resolve(message,sender);if(!url)return null;
    const page=publicUrl(message.page)||publicUrl(sender.url)||publicUrl(sender.origin)||publicUrl(sender.tab?.url);
    const candidate=this.candidates(sender,page).find(x=>x.url===url);
    const context={};
    if(youtubePage(url)){
      const observed=this.youtube.get(sender.tab?.id);
      if(observed && observed.page===url && observed.document===(sender.documentId||'') && this.now()-observed.at<120000){
        const formats=observed.video.formats.map(f=>{
          // Prefer Chrome's successful URL: the player response can still
          // contain the pre-transformation throttle parameter or cipher URL.
          const stream=[...(this.youtubeMedia.get(sender.tab.id)||[])].reverse().find(x=>{
            const u=new URL(x.url),bytes=Number(u.searchParams.get('clen'));return x.frame===sender.frameId && x.document===(sender.documentId||'') && this.now()-x.at<120000 && f.resourceId && u.searchParams.get('id')===f.resourceId && Number(u.searchParams.get('itag'))===f.itag && Number.isSafeInteger(bytes)&&bytes>0&&(!f.bytes||bytes===f.bytes);
          });return stream?{...f,url:stream.url,bytes:Number(new URL(stream.url).searchParams.get('clen'))}:f;
        });
        const descriptor=youtubeDescriptor({...observed.video,formats},new URL(url).searchParams.get('v'));
        if(descriptor){context.youtube=descriptor;context.referer=url;if(typeof message.userAgent==='string'&&message.userAgent.length<=2048&&!/[\x00-\x1f\x7f]/.test(message.userAgent))context.userAgent=message.userAgent;}
      }
      return {url,context};
    }
    if(candidate || publicUrl(message.source)===url){
      const referer=candidate?.headers.referer||page;
      if(referer?.length<=8192)context.referer=referer;
      if(publicUrl(candidate?.headers.origin))context.origin=candidate.headers.origin;
      if(candidate?.headers.userAgent)context.userAgent=candidate.headers.userAgent;
      context.detectedStream=true;
    }
    return {url,context};
  }
}
