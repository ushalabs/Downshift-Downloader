(() => {
  const chrome=globalThis.browser ?? globalThis.chrome;
  if (globalThis.__downshiftVideoButtons) return;
  globalThis.__downshiftVideoButtons = true;
  const players = new Map();
  const pendingYouTube=new Map();
  const pageOrigin=globalThis.origin ?? location.origin;
  addEventListener("message",event=>{
    const value=event.data;
    if(event.source!==window || event.origin!==pageOrigin)return;
    if(value?.type==='downshift-youtube-prepared'){
      const complete=pendingYouTube.get(value.nonce);if(!complete)return;pendingYouTube.delete(value.nonce);
      if(value.video&&/^[\w-]{11}$/.test(value.video.id))chrome.runtime.sendMessage({type:'youtube-observed',page:'https://www.youtube.com/watch?v='+value.video.id,video:value.video}).catch(()=>{}).finally(()=>complete(value.video));else complete(null);return;
    }
    if(value?.type==='downshift-youtube-observed'){
      chrome.runtime.sendMessage({type:'youtube-observed',page:location.href,video:value.video}).catch(()=>{});return;
    }
    if(value?.type!=="downshift-manifest-observed" || typeof value.url!=="string" || value.url.length>32768 || !["hls","dash"].includes(value.format))return;
    chrome.runtime.sendMessage({type:"media-observed",page:location.href,url:value.url,format:value.format,master:value.master===true}).catch(()=>{});
  });
  postMessage({type:"downshift-manifest-replay"},pageOrigin==="null"?"*":pageOrigin);
  let scheduled = false;
  function requestLayout() { if (!scheduled) { scheduled = true; requestAnimationFrame(layout); } }
  const cardSelector='ytd-rich-item-renderer,ytd-video-renderer,ytd-reel-item-renderer,ytd-compact-video-renderer,yt-lockup-view-model';
  function youtubeLink(card){return [...card.querySelectorAll('a[href]')].map(a=>a.href).find(value=>{try{const u=new URL(value);return /(^|\.)youtube\.com$/.test(u.hostname) && (/^[\w-]{11}$/.test(u.searchParams.get('v') || '') || /^\/shorts\/[\w-]{11}(?:\/|$)/.test(u.pathname));}catch{return false;}}) || '';}
  function clickedSource(video){
    if(!video)return '';
    const current=video.currentSrc || video.src;if(/^https?:\/\//i.test(current))return current;
    const sources=[...video.querySelectorAll('source[src]')].map(source=>({src:source.src}));
    const encoded=video.getAttribute('data-sources');if(encoded && encoded.length<=65536)try{const list=JSON.parse(encoded);if(Array.isArray(list))sources.push(...list.slice(0,32));}catch{}
    return sources.filter(source=>typeof source?.src==='string' && /^https?:\/\//i.test(source.src) && source.src.length<=32768).sort((a,b)=>(Number(b.height)||Number(b['data-bitrate'])||0)-(Number(a.height)||Number(a['data-bitrate'])||0))[0]?.src || current || '';
  }
  function identity(target,cardMode){
    if(cardMode)return youtubeLink(target);
    if(/(^|\.)youtube\.com$/.test(location.hostname))return location.pathname+'?v='+(new URL(location.href).searchParams.get('v') || '');
    return clickedSource(target) || location.href;
  }
  function attach(target,cardMode=false) {
    if (players.has(target) || players.size >= 96) return;
    if(!cardMode && target.closest(cardSelector) && /(^|\.)youtube\.com$/.test(location.hostname))return;
    if(!cardMode && /(^|\.)youtube\.com$/.test(location.hostname) && target.closest('ytd-video-preview,#inline-preview-player') && document.querySelector(cardSelector))return;
    const host = document.createElement("div"); host.dataset.downshiftVideo = "true";
    host.style.cssText = "all:initial;position:fixed;z-index:2147483647;display:none;pointer-events:none";
    const root = host.attachShadow({mode:"closed"});
    const style = document.createElement("style");
    style.textContent = `:host{color-scheme:light}.controls{position:relative;width:40px;height:40px;pointer-events:auto}button{all:initial;box-sizing:border-box;display:block;cursor:pointer;border:0;border-radius:50%;font-family:'Segoe UI',sans-serif}.download{width:40px;height:40px;background:transparent;transition:filter .15s}.download svg{display:block;width:100%;height:100%;pointer-events:none}.download:hover{filter:brightness(1.08)}button:focus-visible{outline:2px solid white;outline-offset:2px}.download:disabled{cursor:wait;filter:saturate(.5)}.dismiss{position:absolute;right:-5px;top:-5px;width:18px;height:18px;background:#29212b;color:white;text-align:center;font:16px/18px 'Segoe UI',sans-serif;opacity:0;pointer-events:none;transition:opacity .12s}.controls:hover .dismiss,.controls:focus-within .dismiss{opacity:1;pointer-events:auto}.dismiss:hover{background:#a91858}.notice{pointer-events:auto;position:absolute;right:0;top:46px;box-sizing:border-box;width:max-content;max-width:min(260px,calc(100vw - 24px));padding:7px;border-radius:5px;background:#0f172a;color:white;font:12px/16px 'Segoe UI',sans-serif}.notice:empty{display:none}@media(hover:none){.dismiss{opacity:1;pointer-events:auto}}@media(prefers-reduced-motion:reduce){button{transition:none}}`;
    const controls=document.createElement('div');controls.className='controls';
    const button = document.createElement("button"); button.type = "button";button.className='download';
    button.setAttribute('aria-label','Download this video');
    // Vector rendering of the supplied circular icon: no rectangular background.
    button.innerHTML='<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 64 64" aria-hidden="true"><defs><clipPath id="disc"><circle cx="32" cy="32" r="32"/></clipPath></defs><circle cx="32" cy="32" r="32" fill="#ff73a5"/><g clip-path="url(#disc)" fill="#e65d8c"><path d="M34 14 64 44V64H32L20 50l8-8-8-11 3-3 7 7V14z"/><path d="m44 46 20 20H40L20 50z"/></g><path fill="white" d="M30 14h4v21l7-7 3 3-12 12-12-12 3-3 7 7V14zM20 46h24v4H20z"/></svg>';
    button.title = "Choose quality and download with Downshift";
    const dismiss=document.createElement('button');dismiss.type='button';dismiss.className='dismiss';dismiss.textContent='×';dismiss.title='Hide download control for this video';dismiss.setAttribute('aria-label',dismiss.title);
    const notice = document.createElement("span"); notice.className='notice';notice.setAttribute("role","status");
    controls.append(button,dismiss);root.append(style,controls,notice); document.documentElement.append(host);
    const record = {host, button, notice,cardMode,dismissed:false,identity:identity(target,cardMode)}; players.set(target,record);
    dismiss.addEventListener('click',event=>{
      if(!event.isTrusted)return;
      event.preventDefault();event.stopPropagation();record.dismissed=true;clearTimeout(record.noticeTimer);notice.textContent='';host.style.display='none';
    });
    button.addEventListener("click", async event => {
      // A page script cannot silently ask the native application to download a video.
      if (!event.isTrusted) return;
      event.preventDefault(); event.stopPropagation(); button.disabled = true;clearTimeout(record.noticeTimer);notice.textContent = "Opening Downshift…";
      try {
        const video=cardMode ? target.querySelector('video'):target;
        const card = cardMode ? target:video.closest(cardSelector);
        const player = video?.closest(".html5-video-player");
        let videoPage = (card ? youtubeLink(card):'') || player?.querySelector("a.ytp-title-link[href]")?.href || "";
        if(/(^|\.)linkedin\.com$/.test(location.hostname)){
          for(let post=video?.parentElement,depth=0;post && depth<12;post=post.parentElement,depth++){
            const urn=['data-urn','data-activity-urn','data-id','data-entity-urn'].map(name=>post.getAttribute(name)||'').join(' ').match(/urn:li:activity:(\d+)/);
            if(urn){videoPage='https://www.linkedin.com/feed/update/urn:li:activity:'+urn[1];break;}
            if(post.matches('.feed-shared-update-v2,.occludable-update')){videoPage=[...post.querySelectorAll('a[href]')].map(a=>a.href).find(href=>/^https:\/\/(?:www\.)?linkedin\.com\/(?:feed\/update\/urn:li:activity:\d+|posts\/)/.test(href)) || '';break;}
          }
        }
        let browserPrepared=false;
        if(video && !cardMode && window===top && /(^|\.)youtube\.com$/.test(location.hostname) && !video.mediaKeys){
          notice.textContent='Reading the playing video…';
          let videoId;try{const u=new URL(videoPage||location.href);videoId=u.searchParams.get('v')||u.pathname.match(/^\/(?:shorts|live)\/([\w-]{11})/)?.[1];}catch{}
          const prepared=await new Promise(resolve=>{const nonce=crypto.randomUUID(),timer=setTimeout(()=>{pendingYouTube.delete(nonce);resolve(undefined);},12000);video.setAttribute('data-downshift-request',nonce);pendingYouTube.set(nonce,value=>{clearTimeout(timer);resolve(value);});postMessage({type:'downshift-youtube-prepare',nonce,videoId},location.origin);});
          video.removeAttribute('data-downshift-request');
          // Browser playback metadata is optional. A canonical watch URL can
          // still be resolved by the normal native extractor when Chrome uses
          // SABR, has no standard stream, or the page bridge did not respond.
          if(prepared){videoPage='https://www.youtube.com/watch?v='+prepared.id;browserPrepared=true;}
        }
        const playerCount=[...document.querySelectorAll('video')].filter(v=>v.isConnected && v.getBoundingClientRect().width>=120 && v.getBoundingClientRect().height>=70).length;
        const answer = await chrome.runtime.sendMessage({type:"media", page:location.href, videoPage, browserPrepared, source:clickedSource(video), userAgent:navigator.userAgent, playerCount, protected:!!video?.mediaKeys, title:document.title.slice(0,200)});
        notice.textContent = answer?.ok ? "Choose quality in Downshift." : answer?.error || "Open Downshift and try again.";
        if(answer?.ok)record.noticeTimer=setTimeout(()=>{notice.textContent='';},4000);
      } catch(error) { notice.textContent = error?.message || "Reload this page after reloading the Downshift extension."; }
      button.disabled = false;
    });
    if(!cardMode){target.addEventListener("loadedmetadata",requestLayout); target.addEventListener("emptied",requestLayout);}
  }
  function discover() {
    for (const [video, record] of players) if (!video.isConnected) { record.host.remove(); players.delete(video); }
    // Include players inside open shadow roots without injecting code into the page's main world.
    function scan(root) {if(/(^|\.)youtube\.com$/.test(location.hostname))for(const card of root.querySelectorAll(cardSelector))if(!card.parentElement?.closest(cardSelector) && youtubeLink(card))attach(card,true);for (const video of root.querySelectorAll("video")) attach(video); for (const node of root.querySelectorAll("*")) if (node.shadowRoot) scan(node.shadowRoot); }
    scan(document); requestLayout();
  }
  function layout() {
    scheduled = false;
    for (const [target, record] of players) {
      const {host,cardMode}=record;
      const currentIdentity=identity(target,cardMode);
      if(record.identity!==currentIdentity){record.identity=currentIdentity;record.dismissed=false;clearTimeout(record.noticeTimer);record.notice.textContent='';}
      const anchor=cardMode ? target.querySelector('a#thumbnail,yt-thumbnail-view-model,yt-thumbnail-view-model a,a[href*="/watch?v="],a[href*="/shorts/"]') || target:target;
      const rect = anchor.getBoundingClientRect(), style = getComputedStyle(anchor);
      const visible = !record.dismissed && target.isConnected && rect.width >= 120 && rect.height >= 70 && rect.bottom > 0 && rect.top < innerHeight && rect.right > 0 && rect.left < innerWidth && style.visibility !== "hidden" && style.display !== "none";
      host.style.display = visible ? "block" : "none";
      if (!visible) continue;
      const fullscreen = document.fullscreenElement;
      const container = fullscreen && fullscreen.contains(target) && fullscreen !== target ? fullscreen : document.documentElement;
      if (host.parentNode !== container) container.append(host);
      host.style.top = `${Math.max(8,rect.top + 8)}px`;
      host.style.left = `${Math.max(8,Math.min(innerWidth - 48,rect.right - 48))}px`;
    }
  }
  const observer = new MutationObserver(mutations => { if (mutations.some(m => m.type === "childList" || m.target.tagName === "VIDEO")) { clearTimeout(discover.timer); discover.timer = setTimeout(discover,100); } });
  observer.observe(document.documentElement,{childList:true,subtree:true,attributes:true,attributeFilter:["src"]});
  addEventListener("scroll",requestLayout,true); addEventListener("resize",requestLayout);
  document.addEventListener("fullscreenchange",requestLayout);
  document.addEventListener('pointerover',requestLayout,true);document.addEventListener('pointerout',requestLayout,true);
  // Covers SPA navigation, resized players, and additions inside existing open shadow roots.
  setInterval(discover,2000); discover();
})();
