(() => {
  if (globalThis.__downshiftVideoButtons) return;
  globalThis.__downshiftVideoButtons = true;
  const players = new Map();
  let scheduled = false;
  function requestLayout() { if (!scheduled) { scheduled = true; requestAnimationFrame(layout); } }
  function attach(video) {
    if (players.has(video) || players.size >= 48) return;
    const host = document.createElement("div"); host.dataset.downshiftVideo = "true";
    host.style.cssText = "all:initial;position:fixed;z-index:2147483647;display:none;pointer-events:none";
    const root = host.attachShadow({mode:"closed"});
    const style = document.createElement("style");
    style.textContent = ":host{color-scheme:light}button{pointer-events:auto;display:block;max-width:260px;padding:9px 13px;border:1px solid #93c5fd;border-radius:7px;background:#be185d;color:white;box-shadow:0 2px 10px #0007;font:600 13px/18px 'Segoe UI',sans-serif;cursor:pointer}button:hover{background:#db2777}button:focus-visible{outline:3px solid white;outline-offset:2px}button:disabled{cursor:wait;background:#475569}span{pointer-events:auto;display:block;max-width:260px;padding:7px;border-radius:5px;background:#0f172a;color:white;font:12px/16px 'Segoe UI',sans-serif}span:empty{display:none}";
    const button = document.createElement("button"); button.type = "button"; button.textContent = "↓ Download this video";
    button.title = "Choose quality and download with Downshift";
    const notice = document.createElement("span"); notice.setAttribute("role","status");
    root.append(style,button,notice); document.documentElement.append(host);
    const record = {host, button, notice}; players.set(video,record);
    button.addEventListener("click", async event => {
      // A page script cannot silently ask the native application to download a video.
      if (!event.isTrusted) return;
      event.preventDefault(); event.stopPropagation(); button.disabled = true; notice.textContent = "Opening Downshift…";
      try {
        const answer = await chrome.runtime.sendMessage({type:"media", source:video.currentSrc || video.src || video.querySelector("source")?.src || "", title:document.title.slice(0,200)});
        notice.textContent = answer?.ok ? "Choose quality in Downshift." : answer?.error || "Open Downshift and try again.";
      } catch { notice.textContent = "Reload this page after reloading the Downshift extension."; }
      button.disabled = false;
    });
    video.addEventListener("loadedmetadata",requestLayout); video.addEventListener("emptied",requestLayout);
  }
  function discover() {
    for (const [video, record] of players) if (!video.isConnected) { record.host.remove(); players.delete(video); }
    // Include players inside open shadow roots without injecting code into the page's main world.
    function scan(root) { for (const video of root.querySelectorAll("video")) attach(video); for (const node of root.querySelectorAll("*")) if (node.shadowRoot) scan(node.shadowRoot); }
    scan(document); requestLayout();
  }
  function layout() {
    scheduled = false;
    for (const [video, {host}] of players) {
      const rect = video.getBoundingClientRect(), style = getComputedStyle(video);
      const visible = video.isConnected && rect.width >= 120 && rect.height >= 70 && rect.bottom > 0 && rect.top < innerHeight && rect.right > 0 && rect.left < innerWidth && style.visibility !== "hidden" && style.display !== "none";
      host.style.display = visible ? "block" : "none";
      if (!visible) continue;
      const fullscreen = document.fullscreenElement;
      const container = fullscreen && fullscreen.contains(video) && fullscreen !== video ? fullscreen : document.documentElement;
      if (host.parentNode !== container) container.append(host);
      host.style.top = `${Math.max(8,rect.top + 8)}px`;
      host.style.left = `${Math.max(8,Math.min(innerWidth - 200,rect.right - 196))}px`;
    }
  }
  const observer = new MutationObserver(mutations => { if (mutations.some(m => m.type === "childList" || m.target.tagName === "VIDEO")) { clearTimeout(discover.timer); discover.timer = setTimeout(discover,100); } });
  observer.observe(document.documentElement,{childList:true,subtree:true,attributes:true,attributeFilter:["src"]});
  addEventListener("scroll",requestLayout,true); addEventListener("resize",requestLayout);
  document.addEventListener("fullscreenchange",requestLayout);
  // Covers SPA navigation, resized players, and additions inside existing open shadow roots.
  setInterval(discover,2000); discover();
})();
