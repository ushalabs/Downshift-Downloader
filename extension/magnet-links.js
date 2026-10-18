// Only explicit, trusted link activation opens the native file-selection dialog.
(() => {
  let enabled=false;
  chrome.storage.local.get('handoffState').then(value=>{enabled=value.handoffState?.enabled!==false;});
  chrome.storage.onChanged.addListener((changes,area)=>{if(area==='local'&&changes.handoffState)enabled=changes.handoffState.newValue?.enabled!==false;});
  function notice(text){
    document.querySelector('[data-downshift-magnet]')?.remove();
    const box=document.createElement('div');box.dataset.downshiftMagnet='';box.setAttribute('role','status');
    box.style.cssText='position:fixed;bottom:20px;right:20px;z-index:2147483647;background:white;color:#18243c;border:1px solid #ed1689;border-radius:10px;padding:14px 18px;font:14px sans-serif;box-shadow:0 4px 24px #0003;max-width:360px';
    box.textContent=text;(document.body||document.documentElement).append(box);setTimeout(()=>box.remove(),10000);
  }
  window.addEventListener('click',event=>{
    if(!enabled||!event.isTrusted||event.button!==0||event.ctrlKey||event.metaKey||event.shiftKey||event.altKey)return;
    const link=event.composedPath().find(node=>node instanceof HTMLAnchorElement);
    if(!link||!/^magnet:\?/i.test(link.href))return;
    event.preventDefault();event.stopImmediatePropagation();
    notice('Opening torrent in Downshift…');
    chrome.runtime.sendMessage({type:'magnet',url:link.href}).then(answer=>{
      notice(answer?.ok?'Torrent opened in Downshift. Read metadata to choose files.':answer?.error||'Open Downshift and retry this magnet link.');
    }).catch(()=>notice('Reload the Downshift extension and this page, then retry.'));
  },true);
})();
