// Actual Chrome for Testing + MV3 + Windows registry + Native Messaging + named pipe + engine.
const {createRequire}=require('node:module');
const requireTools=createRequire(process.env.DOWNSHIFT_NODE_MODULES ? require('node:path').join(process.env.DOWNSHIFT_NODE_MODULES,'package.json') : __filename);
const {chromium}=requireTools('playwright');
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),crypto=require('node:crypto'),assert=require('node:assert/strict'),readline=require('node:readline');
const [chrome,driver,hostInput,python,output]=process.argv.slice(2).map(value=>path.resolve(value)),repo=path.resolve(__dirname,'..');
fs.mkdirSync(output,{recursive:true});const root=fs.mkdtempSync(path.join(output,'run-')),native=path.join(root,'native'),downloads=path.join(root,'chrome-files');fs.mkdirSync(native);fs.mkdirSync(downloads);
const {host,extension}=require('./IsolatedBrowser.cjs').isolatedBrowser(repo,root,hostInput);
const registryScript=path.join(repo,'tests','ChromeRegistry.py');let fixture,service,context;const children=[];let checks=0;
function child(exe,args){const p=cp.spawn(exe,args,{stdio:['pipe','pipe','pipe'],env:{...process.env,no_proxy:'localhost,127.0.0.1',NO_PROXY:'localhost,127.0.0.1'}});children.push(p);p.stderr.on('data',d=>process.stderr.write(d));return p;}
function lineReader(p){
  const reader=readline.createInterface({input:p.stdout}),queued=[],pending=[];let failure;
  reader.on('line',line=>pending.length ? pending.shift().resolve(line):queued.push(line));
  const fail=error=>{failure=error;for(const waiter of pending.splice(0))waiter.reject(error);};
  p.on('error',fail);reader.on('close',()=>fail(Error(`Test child output closed (exit ${p.exitCode})`)));
  return ()=>queued.length ? Promise.resolve(queued.shift()):failure ? Promise.reject(failure):new Promise((resolve,reject)=>{
    const timer=setTimeout(()=>fail(Error('Test child response timed out')),45000);
    pending.push({resolve:value=>{clearTimeout(timer);resolve(value);},reject:error=>{clearTimeout(timer);reject(error);}});
  });
}
async function until(test,timeout=30000){const end=Date.now()+timeout;while(Date.now()<end){if(await test())return;await new Promise(r=>setTimeout(r,100));}throw Error('Chrome condition timed out');}
function digest(file){return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');}
const expected=crypto.createHash('sha256');for(let i=0;i<4*1024*1024/256;i++)expected.update(Buffer.from(Array.from({length:256},(_,i)=>i)));const checksum=expected.digest('hex');
(async()=>{
  try {
    const videoFile=path.join(root,'fixture.mp4');const ffmpeg=path.join(path.dirname(host),'media-tools','ffmpeg.exe');const video=cp.spawnSync(ffmpeg,['-hide_banner','-loglevel','error','-f','lavfi','-i','testsrc=size=320x240:rate=12','-f','lavfi','-i','sine=frequency=440','-t','2','-c:v','mpeg4','-pix_fmt','yuv420p','-c:a','aac',videoFile],{encoding:'utf8',timeout:30000});assert.equal(video.status,0,video.stderr);
    fixture=child(python,[path.join(repo,'tests','ChromeFixture.py'),videoFile]);const fixtureLine=lineReader(fixture);const port=Number(await fixtureLine());const base=`http://127.0.0.1:${port}`;
    const manifest=JSON.parse(fs.readFileSync(path.join(repo,'extension','manifest.json')));const id=crypto.createHash('sha256').update(Buffer.from(manifest.key,'base64')).digest().subarray(0,16).toString('hex').replace(/[0-9a-f]/g,n=>String.fromCharCode(97+parseInt(n,16)));assert.equal(id,'empnpdkfnffpcgnfgdndkbielplcoioe');
    const register=cp.spawnSync(python,[registryScript,'install',host,path.join(root,'host.json'),path.join(root,'registry.json'),'--isolated'],{encoding:'utf8'});assert.equal(register.status,0,register.stderr);
    service=child(driver,[native,'--isolated-browser-test']);let serviceLine=lineReader(service);assert.equal(await serviceLine(),'ready');
    async function allRows(){service.stdin.write('snapshot\n');return JSON.parse(await serviceLine());}
    async function rows(){service.stdin.write('confirm\n');assert.equal(await serviceLine(),'confirmed');return (await allRows()).filter(row=>!row.media);}
    const profile=path.join(root,'profile');fs.mkdirSync(path.join(profile,'Default'),{recursive:true});fs.writeFileSync(path.join(profile,'Default','Preferences'),JSON.stringify({download:{default_directory:downloads,prompt_for_download:false}}));
    const browserOptions={executablePath:chrome,headless:true,acceptDownloads:true,downloadsPath:downloads,ignoreDefaultArgs:['--disable-extensions'],args:[`--disable-extensions-except=${extension}`,`--load-extension=${extension}`,'--no-first-run',`--log-net-log=${path.join(root,'netlog.json')}`]};context=await chromium.launchPersistentContext(profile,browserOptions);
    // Playwright's allowAndName mode skips Chrome's ordinary filename pipeline.
    // Exercise the real extension filename gate with isolated Chrome preferences.
    await (await context.browser().newBrowserCDPSession()).send('Browser.setDownloadBehavior',{behavior:'default',eventsEnabled:true});
    let worker=context.serviceWorkers()[0] || await context.waitForEvent('serviceworker',{timeout:15000});assert.equal(await worker.evaluate(()=>chrome.runtime.id),id);checks++;
    let popup=await context.newPage();await popup.goto(`chrome-extension://${id}/popup.html`);await until(async()=>await popup.locator('#connection').textContent()==='Downshift is ready');assert.equal(await popup.locator('#enabled').isChecked(),true);assert.equal(await popup.locator('#applyUpdate').isVisible(),false);assert.equal((await popup.evaluate(()=>chrome.runtime.sendMessage({type:'status'}))).captureProtocol,4);checks++;
    await popup.locator('#enabled').uncheck();await until(async()=>await worker.evaluate(async()=>(await chrome.storage.local.get('handoffState')).handoffState?.enabled===false));
    let page=await context.newPage();await page.goto(base+'/page');
    async function media(command='media'){service.stdin.write(command+'\n');return JSON.parse(await serviceLine());}
    await page.evaluate(base=>{const v=document.createElement('video');v.src=base+'/video.mp4';v.controls=true;v.style.cssText='display:block;width:480px;height:270px;margin-top:35px';document.body.append(v);},base);
    await until(async()=>await page.locator('[data-downshift-video]').isVisible());
    await page.locator('[data-downshift-video]').click({position:{x:60,y:18}});await until(async()=>{const tasks=await media();return tasks.length===1 && tasks[0].ready && tasks[0].button==='Download video';});checks++;
    await media('media-download');await until(async()=>{const tasks=await media();return tasks[0]?.rate==='Complete';},45000);
    const videoOutput=fs.readdirSync(path.join(native,'Videos')).filter(name=>name.endsWith('.mp4'));assert.equal(videoOutput.length,1);assert.equal(digest(path.join(native,'Videos',videoOutput[0])),digest(videoFile));checks++;
    const videoHistory=(await allRows()).filter(row=>row.media);assert.equal(videoHistory.length,1);assert.equal(videoHistory[0].status,3);assert.equal(digest(videoHistory[0].destination),digest(videoFile));checks++;
    await media('media-close');await page.evaluate(()=>document.querySelector('video').remove());await until(async()=>await page.locator('[data-downshift-video]').count()===0);checks++;
    async function items(){return await worker.evaluate(()=>chrome.downloads.search({}));}
    async function click(name){const previous=await worker.evaluate(()=>{if(!globalThis.__testCreated){globalThis.__testCreated=[];chrome.downloads.onCreated.addListener(item=>globalThis.__testCreated.push(item));}return globalThis.__testCreated.length;});await page.locator('#'+name).click();await until(async()=>await worker.evaluate(({previous,name})=>globalThis.__testCreated.slice(previous).some(d=>d.url.includes('kind='+name)),{previous,name}));return await worker.evaluate(({previous,name})=>globalThis.__testCreated.slice(previous).find(d=>d.url.includes('kind='+name)),{previous,name});}
    // Disabled extension leaves a real browser download untouched.
    let item=await click('stream');await until(async()=>(await items()).find(d=>d.id===item.id)?.state==='complete');const historicalItem=(await items()).find(d=>d.id===item.id);assert.equal((await rows()).length,0);assert.equal(digest(historicalItem.filename),checksum);checks++;
    await popup.locator('#enabled').check();await until(async()=>await worker.evaluate(async()=>!!(await chrome.storage.local.get('handoffState')).handoffState?.enabled));
    // Enabled public transfer cancels Chrome only after native durable preparation, then finishes once.
    item=await click('public');await until(async()=>(await rows()).some(r=>r.status===3));let nativeRows=await rows();assert.equal(nativeRows.length,1);assert.equal(digest(nativeRows[0].destination),checksum);await until(async()=>!(await items()).find(d=>d.id===item.id));checks++;
    await popup.evaluate(()=>chrome.runtime.sendMessage({type:'reconcile'}));assert.equal((await rows()).length,1);checks++;
    // Restart actual Chrome (and its MV3 worker) while durable preparation is pending.
    // Hold the next browser storage transition AFTER actual native preparation
    // replies, before cancellation is authorized. All other storage calls remain
    // real. Browser shutdown destroys this test-only gate with the old worker.
    await worker.evaluate(()=>{
      const save=chrome.storage.local.set.bind(chrome.storage.local);
      chrome.storage.local.set=async values=>{
        if(Object.values(values.handoffState?.pending || {}).some(r=>r.phase==='prepared')) {
          globalThis.restartPreparedBlocked=true;await new Promise(()=>{});
        }
        return save(values);
      };
    });
    item=await click('restart');await until(async()=>await worker.evaluate(async()=>Object.values((await chrome.storage.local.get('handoffState')).handoffState.pending).some(r=>r.phase==='preparing')));
    await until(async()=>await worker.evaluate(()=>!!globalThis.restartPreparedBlocked));
    nativeRows=await rows();assert.equal(nativeRows.length,2);assert.equal(nativeRows.filter(r=>r.status===6).length,1);
    const previousWorker=worker;await context.close();context=await chromium.launchPersistentContext(path.join(root,'profile'),browserOptions);
    await (await context.browser().newBrowserCDPSession()).send('Browser.setDownloadBehavior',{behavior:'default',eventsEnabled:true});
    worker=context.serviceWorkers()[0] || await context.waitForEvent('serviceworker',{timeout:15000});assert.notEqual(worker,previousWorker);
    popup=await context.newPage();await popup.goto(`chrome-extension://${id}/popup.html`);page=await context.newPage();await page.goto(base+'/page');
    // Closing this test browser cancels its live download. A replacement worker must
    // abort the inert native preparation, rather than commit an unrelated cancellation.
    await until(async()=>await worker.evaluate(async()=>Object.keys((await chrome.storage.local.get('handoffState')).handoffState.pending).length===0));
    nativeRows=await rows();assert.equal(nativeRows.length,2);assert.equal(nativeRows.filter(r=>r.status===3).length,1);assert.equal(nativeRows.filter(r=>r.status===5).length,1);checks++;
    // Restored completed Chrome history must remain history, not open a new
    // Downshift save dialog. Query/icon reads can produce metadata events.
    await worker.evaluate(async id=>{await chrome.downloads.search({});try{await chrome.downloads.getFileIcon(id);}catch{}},historicalItem.id);await popup.evaluate(()=>chrome.runtime.sendMessage({type:'reconcile'}));
    assert.equal((await rows()).length,2);assert((await items()).some(d=>d.id===historicalItem.id));assert.equal(digest(historicalItem.filename),checksum);checks++;
    item=await click('public');await until(async()=>(await rows()).filter(r=>r.status===3).length===2);nativeRows=await rows();assert.equal(nativeRows.length,3);for(const r of nativeRows.filter(r=>r.status===3))assert.equal(digest(r.destination),checksum);await until(async()=>!(await items()).find(d=>d.id===item.id));checks++;
    // The actual response is resolved before selecting the native range engine.
    const beforeDirect=(await items()).map(d=>d.id);item=await click('direct');await until(async()=>(await rows()).filter(r=>r.status===3).length===3);nativeRows=await rows();assert.equal(nativeRows.length,4);assert.equal(digest(nativeRows.find(r=>r.url.includes('kind=direct')).destination),checksum);await until(async()=>!(await items()).find(d=>d.id===item.id));assert.deepEqual((await items()).map(d=>d.id),beforeDirect);checks++;
    // Cookie-only response now finishes in Downshift, without exporting cookies.
    await context.addCookies([{name:'fixture_session',value:'authorized',url:base}]);item=await click('auth');await until(async()=>(await rows()).some(r=>r.url.includes('kind=auth') && r.status===3));const authRow=(await rows()).find(r=>r.url.includes('kind=auth'));assert.equal(authRow.browserTransfer,true);assert.equal(digest(authRow.destination),checksum);await until(async()=>!(await items()).find(d=>d.id===item.id));checks++;
    // A tiny opaque attachment cannot finish during asynchronous native preflight.
    item=await click('tiny');await until(async()=>(await rows()).some(r=>r.url.includes('/tiny') && r.status===3));
    nativeRows=await rows();assert.equal(path.basename(nativeRows.find(r=>r.url.includes('/tiny')).destination),'tiny.rbxmx');assert.equal(digest(nativeRows.find(r=>r.url.includes('/tiny')).destination),crypto.createHash('sha256').update('Downshift tiny attachment\n').digest('hex'));await until(async()=>!(await items()).find(d=>d.id===item.id));checks++;
    // Unknown file suffixes are captured through the same response pipeline.
    item=await click('unusual');await until(async()=>(await rows()).some(r=>r.url.includes('/asset.rbxmx') && r.status===3));assert.equal(digest((await rows()).find(r=>r.url.includes('/asset.rbxmx')).destination),checksum);await until(async()=>!(await items()).find(d=>d.id===item.id));checks++;
    item=await click('redirect');await until(async()=>(await rows()).some(r=>r.url.includes('kind=redirect') && r.status===3));assert.equal(digest((await rows()).find(r=>r.url.includes('kind=redirect')).destination),checksum);await until(async()=>!(await items()).find(d=>d.id===item.id));checks++;
    // Browser-owned blob bytes are delivered and verified through Downshift.
    const blobBefore=await worker.evaluate(()=>globalThis.__testCreated.length);await page.evaluate(()=>{const a=document.createElement('a');a.id='blob-file';a.download='browser-only.rbxmx';a.href=URL.createObjectURL(new Blob(['browser-only generated file']));a.textContent='Blob attachment';document.body.append(a);});await page.locator('#blob-file').click();
    await until(async()=>await worker.evaluate(n=>globalThis.__testCreated.length>n,blobBefore));const blobItem=await worker.evaluate(n=>globalThis.__testCreated[n],blobBefore);await until(async()=>(await rows()).some(r=>r.url.startsWith('blob:') && r.status===3));const blobRow=(await rows()).find(r=>r.url.startsWith('blob:'));assert.equal(blobRow.browserTransfer,true);assert.equal(fs.readFileSync(blobRow.destination,'utf8'),'browser-only generated file');await until(async()=>!(await items()).find(d=>d.id===blobItem.id));checks++;
    await page.evaluate(()=>{const a=document.createElement('a');a.id='data-file';a.download='inline.rbxmx';a.href='data:application/octet-stream;base64,'+btoa('inline data file');a.textContent='Inline file';document.body.append(a);});await page.locator('#data-file').click();await until(async()=>(await rows()).some(r=>r.url.startsWith('data:') && r.status===3));assert.equal(fs.readFileSync((await rows()).find(r=>r.url.startsWith('data:')).destination,'utf8'),'inline data file');checks++;
    await page.evaluate(base=>{const form=document.createElement('form');form.method='POST';form.action=base+'/post-response';const input=document.createElement('input');input.name='token';input.value='fixture-body';const submit=document.createElement('button');submit.id='post-file';submit.textContent='POST download';form.append(input,submit);document.body.append(form);},base);await page.locator('#post-file').click();await until(async()=>(await rows()).some(r=>r.url.includes('/post-response') && r.status===3));assert.equal(digest((await rows()).find(r=>r.url.includes('/post-response')).destination),crypto.createHash('sha256').update(Buffer.from('original authorized POST response\0'.repeat(8192))).digest('hex'));await until(async()=>await worker.evaluate(async()=>Object.keys((await chrome.storage.local.get('handoffState')).handoffState.pending).length===0));checks++;
    // A revoked object URL remains downloadable: no second blob fetch is used.
    await page.evaluate(()=>{const bytes=new Uint8Array(2*1024*1024);for(let i=0;i<bytes.length;i++)bytes[i]=i%256;const a=document.createElement('a');a.id='revoked-file';a.download='revoked.rbxmx';const url=a.href=URL.createObjectURL(new Blob([bytes]));a.onclick=()=>setTimeout(()=>URL.revokeObjectURL(url),20);a.textContent='Revoked blob';document.body.append(a);});await page.locator('#revoked-file').click();await until(async()=>(await rows()).some(r=>r.destination.endsWith('revoked.rbxmx') && r.status===3));assert.equal(digest((await rows()).find(r=>r.destination.endsWith('revoked.rbxmx')).destination),crypto.createHash('sha256').update(Buffer.alloc(2*1024*1024).map((_,i)=>i%256)).digest('hex'));checks++;
    // Native Pause/Resume controls the original cookie-bound response. Restart
    // the actual app while paused, then resume without creating a duplicate row.
    await page.evaluate(base=>{const a=document.createElement('a');a.id='pause-source';a.href=base+'/range?kind=pause';a.textContent='Managed pause';document.body.append(a);},base);await page.locator('#pause-source').click();let pauseRow;
    await until(async()=>{pauseRow=(await rows()).find(r=>r.url.includes('kind=pause'));return pauseRow?.status===1 && Number(pauseRow.downloaded)>0;});
    service.stdin.write('pause '+pauseRow.id+'\n');assert(Array.isArray(JSON.parse(await serviceLine())));await until(async()=>(await items()).some(d=>d.url.includes('kind=pause') && d.paused));checks++;
    service.stdin.write('exit\n');await new Promise(resolve=>service.once('exit',resolve));service=child(driver,[native,'--isolated-browser-test']);serviceLine=lineReader(service);assert.equal(await serviceLine(),'ready');
    service.stdin.write('resume '+pauseRow.id+'\n');assert(Array.isArray(JSON.parse(await serviceLine())));await until(async()=>(await rows()).find(r=>r.id===pauseRow.id)?.status===3);assert.equal((await rows()).filter(r=>r.url.includes('kind=pause')).length,1);assert.equal(digest((await rows()).find(r=>r.id===pauseRow.id).destination),checksum);await until(async()=>await worker.evaluate(async()=>Object.keys((await chrome.storage.local.get('handoffState')).handoffState.pending).length===0));checks++;
    // Native unavailable: enabled capture holds the file; disabling restores Chrome.
    const acceptedRows=await rows();service.stdin.write('exit\n');await new Promise(resolve=>service.once('exit',resolve));service=null;
    // Simulate a genuinely unavailable installation: otherwise default takeover now
    // launches the adjacent real app and would escape the isolated fixture queue.
    const unavailable=cp.spawnSync(python,[registryScript,'install',path.join(root,'missing-native-host.exe'),path.join(root,'unavailable-host.json'),path.join(root,'unavailable-backup.json'),'--isolated'],{encoding:'utf8'});assert.equal(unavailable.status,0,unavailable.stderr);
    await popup.locator('#enabled').check();item=await click('public');await until(async()=>(await items()).find(d=>d.id===item.id)?.paused===true);checks++;await popup.locator('#enabled').uncheck();await until(async()=>(await items()).find(d=>d.id===item.id)?.state==='complete');assert.equal(digest((await items()).find(d=>d.id===item.id).filename),checksum);checks++;
    await popup.reload();await until(async()=>await popup.locator('#connection').textContent()==='Open Downshift to connect');await popup.locator('body').screenshot({path:path.join(output,'popup.png')});
    const fingerprint=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
    const sources=['CMakeLists.txt','src/browser/NativeProtocol.cpp','src/browser/PipeServer.cpp','src/browser/NativeHost.cpp','src/core/DownloadQueue/QueueManager.cpp','src/database/QueueStore.cpp','src/core/DownloadEngine/DownloadEngine.cpp','src/core/ConnectionManager/AdaptiveController.h','src/gui/MainWindow.cpp','src/gui/Dialogs.cpp','src/gui/MediaDialog.cpp','extension/manifest.json','extension/background.js','extension/file-links.js','extension/handoff.js','extension/browser-transfer.js','extension/media.js','extension/video-button.js','extension/popup.html','extension/popup.js','tests/IsolatedBrowser.cjs','tests/BrowserDriver.cpp','tests/ChromeTests.cjs','tests/ChromeFixture.py','tests/ChromeRegistry.py'];
    fs.writeFileSync(path.join(output,'report.json'),JSON.stringify({chromeVersion:context.browser()?.version(),playwrightVersion:requireTools('playwright/package.json').version,extensionId:id,checks,extensionVersion:manifest.version,nativeFiles:acceptedRows.filter(r=>r.status===3).length,queueRecords:acceptedRows.length,browserTransportRecords:acceptedRows.filter(r=>r.browserTransfer).length,ownedCancelledEntriesRemoved:true,mediaRecords:1,independentSha256:checksum,profile:'isolated',nativeBridge:'separate test host and pipe; normal registration untouched',browserExecutableSha256:fingerprint(chrome),nativeHostSha256:fingerprint(host),nativeGuiDriverSha256:fingerprint(driver),sources:Object.fromEntries(sources.map(file=>[file,fingerprint(path.join(repo,file))])),scope:'Public strong-validator responses use native HTTP; generated blob/data, cookie-required GET, POST, tiny and unlisted-suffix responses finish in Downshift with independently verified bytes. Original browser request is retained for session-bound transfers. Verified transports are cleaned after native completion. Existing video, worker restart, explicit-off and outage regressions remain.'},null,2));
    console.log(`Actual Chrome checks passed: ${checks}`);
  } catch(error) {
    if(context) {try{const worker=context.serviceWorkers()[0];fs.writeFileSync(path.join(output,'failure.json'),JSON.stringify(await worker.evaluate(async()=>({downloads:await chrome.downloads.search({}),storage:await chrome.storage.local.get('handoffState')})),null,2));}catch{}}
    throw error;
  } finally {
    if(context)await context.close();for(const p of children){if(p.exitCode===null)p.kill();}
    cp.spawnSync(python,[registryScript,'restore',path.join(root,'registry.json'),'--isolated'],{encoding:'utf8'});
  }
})().catch(error=>{console.error(error);process.exitCode=1;});
