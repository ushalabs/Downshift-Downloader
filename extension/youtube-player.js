// This runs only on YouTube. Session cookies remain inside Chrome's own fetch;
// only bounded, non-DRM media descriptors are sent to the extension/native app.
(() => {
  if(globalThis.__downshiftYouTubePlayer || !/(^|\.)youtube\.com$/.test(location.hostname) || window!==top)return;
  globalThis.__downshiftYouTubePlayer=true;
  let previous='',reported=0,busy=false,lastAttempt=0,lastAttemptId='',cached=null;
  const currentId=()=>new URL(location.href).searchParams.get('v')||location.pathname.match(/^\/(?:shorts|live)\/([\w-]{11})/)?.[1];
  const unpack=value=>{try{return typeof value==='string'&&value.length<=2097152?JSON.parse(value):value;}catch{return null;}};
  const response=(player=document.getElementById('movie_player'))=>unpack(player?.getPlayerResponse?.())||unpack(globalThis.ytInitialPlayerResponse);
  const validId=id=>typeof id==='string'&&/^[\w-]{11}$/.test(id);
  function describe(r,id=currentId()){
    r=unpack(r);
    if(!validId(id) || r?.videoDetails?.videoId!==id || r.playabilityStatus?.status!=='OK' || r.videoDetails.isLiveContent || r.streamingData?.drmFamilies?.length)return null;
    const formats=[...(r.streamingData?.formats||[]),...(r.streamingData?.adaptiveFormats||[])].slice(0,64)
      .filter(f=>!f.drmFamilies?.length&&!f.drmTrackType&&!f.has_drm)
      // Keep alternate codecs/itags: the browser may have loaded only one of
      // several tracks at the same height. Resolve that actual request later.
      .sort((a,b)=>Number(!!a.height)-Number(!!b.height)||(b.height||0)-(a.height||0)).slice(0,32)
      .map(f=>{const source=f.url||new URLSearchParams(f.signatureCipher||f.cipher||'').get('url');let resourceId='';try{resourceId=new URL(source).searchParams.get('id')||'';}catch{}
        let bytes=Number(f.contentLength);if(!bytes){try{bytes=Number(new URL(source).searchParams.get('clen'));}catch{}}
        return {itag:f.itag,url:f.url,resourceId,mime:String(f.mimeType||'').split(';')[0],height:f.height,bytes,audio:!!f.audioQuality};});
    const video={id,duration:Number(r.videoDetails.lengthSeconds),formats,manifest:r.streamingData?.hlsManifestUrl||r.streamingData?.dashManifestUrl};
    while(JSON.stringify(video).length>42000 && video.formats.length)video.formats.pop();
    return video;
  }
  function report(force=false){try{
    const video=cached?.id===currentId() && Date.now()-cached.at<60000 ? cached.video:describe(response());if(!video || document.querySelector('video')?.mediaKeys)return;
    const packet=JSON.stringify(video);if(!force && packet===previous && Date.now()-reported<30000)return;previous=packet;reported=Date.now();
    postMessage({type:'downshift-youtube-observed',video},location.origin);
  }catch{}}
  const standard=f=>{try{const u=new URL(f.url);return u.protocol==='https:'&&!u.username&&!u.password&&!u.port&&/(^|\.)googlevideo\.com$/.test(u.hostname)&&u.pathname==='/videoplayback'&&u.searchParams.get('ump')!=='1'&&!u.searchParams.has('sabr')&&(!u.searchParams.has('expire')||Number(u.searchParams.get('expire'))*1000>Date.now()+30000)&&Number.isSafeInteger(f.bytes)&&f.bytes>0&&/^(?:audio|video)\/(?:mp4|webm)$/.test(f.mime);}catch{return false;}};
  async function prepare(nonce,expectedId){
    // Bind to the clicked <video>, including home-feed previews and SPA
    // players whose document URL is not a watch URL. Never reuse another card.
    const element=document.querySelector('video[data-downshift-request="'+nonce+'"]');
    const player=element?.closest?.('.html5-video-player')||document.getElementById('movie_player');
    const raw=response(player),data=player?.getVideoData?.(),id=validId(expectedId)?expectedId:validId(data?.video_id)?data.video_id:currentId();
    if(!validId(id) || (validId(data?.video_id)&&data.video_id!==id) || element?.mediaKeys || data?.isLive || raw?.videoDetails?.isLiveContent || raw?.streamingData?.drmFamilies?.length || player?.classList?.contains('ad-showing'))return null;
    let original=describe(raw,id);
    if(!original){
      // Some current player APIs expose only video data and a MediaSource;
      // missing response metadata must not prevent the click-only request.
      if(raw?.videoDetails?.videoId===id&&raw.playabilityStatus?.status&&raw.playabilityStatus.status!=='OK')return null;
      const duration=Number(element?.duration||data?.length_seconds);
      if(!element || data?.video_id!==id || !Number.isFinite(duration)||duration<=0||duration>604800)return null;
      original={id,duration,formats:[]};
    }
    // A legacy muxed 360p stream is not the complete quality list. If higher
    // adaptive tracks are unresolved, ask once for the current player's HLS
    // representation instead of prematurely returning only format 18.
    const playableHeight=Math.max(0,...original.formats.filter(standard).map(f=>f.height||0));
    const advertisedHeight=Math.max(0,...original.formats.map(f=>f.height||0));
    const complete=playableHeight>0 && original.formats.some(f=>standard(f)&&(!f.height||f.audio)) && playableHeight>=advertisedHeight && original.formats.some(f=>standard(f)&&f.height&&!f.audio);
    if(original.manifest || complete)return original;
    if(cached?.id===original.id && Date.now()-cached.at<60000)return cached.video;
    if(busy || (lastAttemptId===id&&Date.now()-lastAttempt<30000))return original;
    // A click can request the public HLS-capable Web player representation
    // using this browser's current visitor context. No anonymous native watch
    // lookup, cookie export, challenge solving or alternative account is used.
    const actual=globalThis.ytcfg?.get?.('INNERTUBE_CONTEXT')?.client;
    if(!actual?.clientVersion)return original;
    busy=true;lastAttempt=Date.now();lastAttemptId=id;const abort=new AbortController(),timeout=setTimeout(()=>abort.abort(),10000);
    try{
      let stamp=Number(globalThis.ytcfg.get('STS')||globalThis.ytplayer?.config?.args?.sts);
      if(!Number.isInteger(stamp)||stamp<=0){
        // Use only a player script the page already loaded. Read its timestamp,
        // never execute or export it, and stop as soon as the value is found.
        const script=Array.from(document.querySelectorAll?.('script[src]')||[]).map(x=>x.src).find(value=>{try{const u=new URL(value,location.href);return u.origin===location.origin&&/\/s\/player\/[^?]+\/base\.js$/.test(u.pathname);}catch{return false;}});
        if(script){
          const asset=await fetch(script,{cache:'force-cache',credentials:'same-origin',mode:'same-origin',redirect:'error',signal:abort.signal});
          if(asset.ok&&asset.body){const reader=asset.body.getReader(),decoder=new TextDecoder();let tail='',bytes=0;try{for(;;){const part=await reader.read();if(part.done)break;bytes+=part.value.byteLength;if(bytes>8388608)break;tail+=decoder.decode(part.value,{stream:true});const found=tail.match(/signatureTimestamp\s*[:=]\s*(\d+)/);if(found){stamp=Number(found[1]);break;}tail=tail.slice(-128);}}finally{reader.cancel().catch(()=>{});}}
        }
      }
      const client={...actual,clientName:'WEB',userAgent:'Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/605.1.15 (KHTML, like Gecko) Version/15.5 Safari/605.1.15,gzip(gfe)'};
      const headers={'Content-Type':'application/json','X-Youtube-Client-Name':'1','X-Youtube-Client-Version':String(actual.clientVersion)};
      const visitor=actual.visitorData||globalThis.ytcfg.get('VISITOR_DATA');if(typeof visitor==='string'&&visitor.length<=2048)headers['X-Goog-Visitor-Id']=visitor;
      // Sign YouTube's own same-origin request inside its page. The cookie and
      // derived authorization never cross the window/extension/native bridge.
      // No cookie API permission, cookie file or account import is involved.
      const cookies=String(document.cookie||'').split(';').map(x=>x.trim());
      let sid='';for(const name of ['SAPISID','__Secure-3PAPISID']){const match=cookies.find(x=>x.startsWith(name+'='));if(match){sid=match.slice(name.length+1);break;}}
      if(sid && sid.length<=4096 && globalThis.crypto?.subtle){
        const timestamp=Math.floor(Date.now()/1000),bytes=new TextEncoder().encode(timestamp+' '+sid+' '+location.origin);
        const digest=Array.from(new Uint8Array(await crypto.subtle.digest('SHA-1',bytes)),x=>x.toString(16).padStart(2,'0')).join('');
        headers.Authorization='SAPISIDHASH '+timestamp+'_'+digest;headers['X-Origin']=location.origin;
        const account=Number(globalThis.ytcfg.get('SESSION_INDEX'));headers['X-Goog-AuthUser']=Number.isInteger(account)&&account>=0&&account<100?String(account):'0';
      }
      const key=globalThis.ytcfg.get('INNERTUBE_API_KEY'),query=typeof key==='string'&&/^[\w-]{20,80}$/.test(key)?'&key='+encodeURIComponent(key):'';
      const result=await fetch('/youtubei/v1/player?prettyPrint=false'+query,{method:'POST',credentials:'include',mode:'same-origin',redirect:'error',signal:abort.signal,
        headers,
        body:JSON.stringify({videoId:original.id,context:{client},playbackContext:{contentPlaybackContext:{html5Preference:'HTML5_PREF_WANTS',...(Number.isInteger(stamp)&&stamp>0?{signatureTimestamp:stamp}:{})}},contentCheckOk:false,racyCheckOk:false})});
      if(!result.ok || !result.body)return original;
      const reader=result.body.getReader(),decoder=new TextDecoder();let text='',bytes=0;try{for(;;){const part=await reader.read();if(part.done)break;bytes+=part.value.byteLength;if(bytes>2097152)return original;text+=decoder.decode(part.value,{stream:true});}text+=decoder.decode();}finally{reader.cancel().catch(()=>{});}
      const video=describe(JSON.parse(text),id);if(video && (video.manifest || video.formats.some(f=>standard(f)&&f.height))){cached={id:original.id,at:Date.now(),video};return video;}
      return original;
    }catch{return original;}finally{clearTimeout(timeout);busy=false;}
  }
  addEventListener('message',event=>{
    if(event.source!==window || event.origin!==location.origin)return;
    if(event.data?.type==='downshift-manifest-replay')report(true);
    if(event.data?.type==='downshift-youtube-prepare' && typeof event.data.nonce==='string' && /^[a-f0-9-]{36}$/.test(event.data.nonce)){
      const nonce=event.data.nonce;prepare(nonce,event.data.videoId).then(video=>postMessage({type:'downshift-youtube-prepared',nonce,video},location.origin)).catch(()=>postMessage({type:'downshift-youtube-prepared',nonce,video:null},location.origin));
    }
  });
  setInterval(()=>report(),1000);
})();
