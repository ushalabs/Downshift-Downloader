// Warm the app on an explicit download click without cancelling navigation,
// guessing URL suffixes or replaying an authenticated/POST request as a GET.
(() => {
  const chrome=globalThis.browser ?? globalThis.chrome;
  if(globalThis.__downshiftFileIntent)return;globalThis.__downshiftFileIntent=true;
  addEventListener('click',event=>{
    if(!event.isTrusted || event.button!==0 || event.ctrlKey || event.metaKey || event.shiftKey || event.altKey)return;
    const anchor=event.composedPath().find(node=>node instanceof HTMLAnchorElement && node.hasAttribute('download'));
    if(anchor && /^(?:https?:|blob:|data:)/i.test(anchor.href))chrome.runtime.sendMessage({type:'download-intent'}).catch(()=>{});
  },true);
})();
