const $=id=>document.getElementById(id);
$("version").textContent=chrome.runtime.getManifest().version;
async function request(message){try{const answer=await chrome.runtime.sendMessage(message);if(!answer?.ok)throw Error(answer?.error || "The extension worker did not reply. Apply update, then refresh this page.");return answer;}catch(error){$("notice").textContent=error.message;$("applyUpdate").hidden=false;return null;}}
function display(state){if(!state)return;$("enabled").checked=state.enabled;$("connection").textContent=state.connected ? "Downshift is ready":"Open Downshift to connect";$("indicator").style.background=state.connected ? "#16a34a":"#94a3b8";$("notice").textContent=state.notice || "";$("pending").textContent=state.pending ? `${state.pending} handoff(s) waiting for confirmation`:"";}
async function refresh(){const state=await request({type:"status"});display(state);if(state){const mismatch=state.connected && state.appVersion!==chrome.runtime.getManifest().version;$("applyUpdate").hidden=state.captureProtocol===4 && !mismatch;if(state.captureProtocol!==4)$("notice").textContent="An extension update is waiting. Apply it, then refresh your pages.";else if(mismatch)$("notice").textContent="App and extension versions differ. Restart Downshift from its updated shortcut, apply the extension update, and refresh your pages.";}}
$("applyUpdate").addEventListener("click",()=>chrome.runtime.reload());
$("enabled").addEventListener("change",async()=>{await request({type:"configure",enabled:$("enabled").checked});await refresh();});
$("open").addEventListener("click",async()=>{await request({type:"open"});await refresh();});
$("retry").addEventListener("click",async()=>{await request({type:"retry"});await refresh();});
refresh();
