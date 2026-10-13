(() => {
  if(globalThis.__downshiftFileLinks)return;globalThis.__downshiftFileLinks=true;
  let enabled=false;
  chrome.storage.local.get('handoffState').then(result=>{enabled=result.handoffState?.enabled!==false;});
  chrome.storage.onChanged.addListener((changes,area)=>{if(area==='local' && changes.handoffState)enabled=changes.handoffState.newValue?.enabled!==false;});
  let notice;
  function show(text){
    if(!notice){const host=document.createElement('div');host.style.cssText='all:initial;position:fixed;right:18px;bottom:18px;z-index:2147483647';const root=host.attachShadow({mode:'closed'});notice=document.createElement('div');notice.style.cssText="max-width:340px;padding:14px 18px;border-radius:12px;background:#be185d;color:white;box-shadow:0 6px 24px #0004;font:14px/20px Segoe UI,sans-serif";notice.setAttribute('role','status');root.append(notice);document.documentElement.append(host);}
    notice.textContent=text;
  }
  document.addEventListener('click',async event=>{
    if(!event.isTrusted || !enabled || event.button!==0 || event.ctrlKey || event.metaKey || event.altKey || event.shiftKey)return;
    const link=event.composedPath().find(node=>node?.tagName==='A' && node.href);if(!link)return;
    let url;try{url=new URL(link.href);}catch{return;}
    if(!/^https?:$/.test(url.protocol) || url.username || url.password)return;
    // Known direct file links and explicit download anchors can be handed over
    // before Chrome starts a download. Unknown server-side attachments retain
    // the durable downloads-API fallback.
    if(!link.hasAttribute('download') && !/\.(?:exe|msi|msix|zip|rar|7z|tar|gz|bz2|xz|iso|pdf|mp4|mkv|webm|mov|avi|mp3|m4a|wav|flac|ogg|docx?|xlsx?|pptx?|csv|txt|jpg|jpeg|png|gif|webp)(?:$)/i.test(url.pathname))return;
    event.preventDefault();event.stopImmediatePropagation();show('Opening download in Downshift…');
    try{const answer=await chrome.runtime.sendMessage({type:'link',url:url.href,filename:link.getAttribute('download') || ''});show(answer?.ok ? 'Choose where to save this file in Downshift.' : answer?.error || 'Could not accept this link. Turn capture off to use Chrome.');}
    catch{show('Reload the Downshift extension and this page, then retry.');}
  },true);
})();
