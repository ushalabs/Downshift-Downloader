const $=id=>document.getElementById(id);
async function request(message){try{const answer=await chrome.runtime.sendMessage(message);if(!answer?.ok)throw new Error();return answer;}catch{$("notice").textContent="Downshift is unavailable. Open the app and check its Chrome setup.";return null;}}
function display(state){if(!state)return;$("enabled").checked=state.enabled;$("connection").textContent=state.connected ? "Downshift is ready":"Open Downshift to connect";$("indicator").style.background=state.connected ? "#16a34a":"#94a3b8";$("notice").textContent=state.notice || "";$("pending").textContent=state.pending ? `${state.pending} handoff(s) waiting for confirmation`:"";}
async function refresh(){display(await request({type:"status"}));}
$("enabled").addEventListener("change",async()=>{await request({type:"configure",enabled:$("enabled").checked});await refresh();});
$("open").addEventListener("click",async()=>{await request({type:"open"});await refresh();});
$("leave").addEventListener("click",async()=>{await request({type:"leave"});await refresh();});
$("retry").addEventListener("click",async()=>{await request({type:"reconcile"});await refresh();});
refresh();
