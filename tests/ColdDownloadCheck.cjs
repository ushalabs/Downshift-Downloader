// One real Chrome download, no running app, actual Native Messaging startup.
const {createRequire}=require('node:module'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),readline=require('node:readline'),assert=require('node:assert/strict'),crypto=require('node:crypto'),http=require('node:http');
const requireTools=createRequire(path.join(process.env.DOWNSHIFT_NODE_MODULES,'package.json')),{chromium}=requireTools('playwright');
const [chrome,driver,hostInput,python,outInput,reference]=process.argv.slice(2,8).map(value=>path.resolve(value)),target=process.argv[8],delay=Number(process.argv[9]||8000),repo=path.resolve(__dirname,'..'),out=outInput;
fs.mkdirSync(out,{recursive:true});const root=fs.mkdtempSync(path.join(out,'run-')),native=fs.mkdtempSync(path.join(require('node:os').tmpdir(),'ds-cold-')),downloads=path.join(root,'chrome-files'),signal=path.join(root,'.native-start-request');fs.mkdirSync(downloads);fs.writeFileSync(path.join(root,'.native-launch-fixture'),'private test');
const {host,extension}=require('./IsolatedBrowser.cjs').isolatedBrowser(repo,root,hostInput),registry=path.join(repo,'tests/ChromeRegistry.py');let service,context,timer,starting,read,fixture;
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
async function until(check,timeout=90000){const end=Date.now()+timeout;while(Date.now()<end){if(await check())return;await new Promise(resolve=>setTimeout(resolve,100));}throw Error('Cold download did not settle after one click');}
async function command(text){service.stdin.write(text+'\n');return await read();}
(async()=>{try{
 const install=cp.spawnSync(python,[registry,'install',host,path.join(root,'host.json'),path.join(root,'registry.json'),'--isolated'],{encoding:'utf8'});assert.equal(install.status,0,install.stderr);
 fixture=http.createServer((req,res)=>{res.setHeader('Content-Type','text/html');res.end('<!doctype html><a id="file" href="'+target.replaceAll('&','&amp;').replaceAll('"','&quot;')+'">Download repository ZIP</a>');});await new Promise(resolve=>fixture.listen(0,'127.0.0.1',resolve));
 let signaledAt,readyAt,clickAt;timer=setInterval(()=>{if(starting || !fs.existsSync(signal))return;signaledAt=Date.now();starting=(async()=>{await new Promise(resolve=>setTimeout(resolve,delay));service=cp.spawn(driver,[native,'--isolated-browser-test'],{stdio:['pipe','pipe','pipe'],env:{...process.env,NO_PROXY:'localhost,127.0.0.1',no_proxy:'localhost,127.0.0.1'}});service.stderr.on('data',data=>process.stderr.write(data));const queue=[],waiting=[];readline.createInterface({input:service.stdout}).on('line',line=>waiting.length?waiting.shift()(line):queue.push(line));read=()=>queue.length?Promise.resolve(queue.shift()):new Promise((resolve,reject)=>{const timeout=setTimeout(()=>reject(Error('Cold driver response timed out')),30000);waiting.push(line=>{clearTimeout(timeout);resolve(line);});});assert.equal(await read(),'ready');readyAt=Date.now();})();},20);
 const profile=process.argv[10]?path.resolve(process.argv[10]):path.join(root,'profile');assert(profile.startsWith(path.join(repo,'build','firstclick-20261104')+path.sep));fs.mkdirSync(path.join(profile,'Default'),{recursive:true});fs.writeFileSync(path.join(profile,'Default','Preferences'),JSON.stringify({download:{default_directory:downloads,prompt_for_download:false}}));
 context=await chromium.launchPersistentContext(profile,{executablePath:chrome,headless:true,acceptDownloads:true,downloadsPath:downloads,ignoreDefaultArgs:['--disable-extensions'],env:{...process.env,DOWNSHIFT_COLD_TEST_SIGNAL:signal},args:[`--disable-extensions-except=${extension}`,`--load-extension=${extension}`,'--no-first-run']});await(await context.browser().newBrowserCDPSession()).send('Browser.setDownloadBehavior',{behavior:'default',eventsEnabled:true});
 const worker=context.serviceWorkers()[0]||await context.waitForEvent('serviceworker');
 const previousFinished=await worker.evaluate(async()=>{
   globalThis.__created=[];
   chrome.downloads.onCreated.addListener(({id,startTime})=>__created.push({id,startTime}));
   return (await chrome.storage.local.get('handoffState')).handoffState?.finished || [];
 });
 const page=await context.newPage();await page.goto('http://127.0.0.1:'+fixture.address().port);clickAt=Date.now();await page.locator('#file').click();
 await until(async()=>!!readyAt);let captures,rows;await until(async()=>{captures=JSON.parse(await command('captures'));rows=JSON.parse(await command('snapshot'));return captures.length===1&&captures[0].ready;},15000);
 assert.equal(rows.length,1);assert.equal(await worker.evaluate(()=>__created.length),1);const promptedAt=Date.now();assert.equal(await command('confirm'),'confirmed');
 await until(async()=>{rows=JSON.parse(await command('snapshot'));if(rows.some(row=>row.status===4))throw Error(rows.find(row=>row.status===4).error);return rows.length===1&&rows[0].status===3;},180000);
 assert.equal(sha(rows[0].destination),sha(reference));await until(async()=>(await worker.evaluate(()=>chrome.downloads.search({}))).length===0);
 const [identity]=await worker.evaluate(()=>__created);
 if(process.env.DOWNSHIFT_COLD_LEGACY_FIXTURE==='1'){
   await worker.evaluate(async id=>{
     const {handoffState}=await chrome.storage.local.get('handoffState');
     await chrome.storage.local.set({handoffState:{...handoffState,finished:[id]}});
   },identity.id);
 }
 const report={url:target,oneClick:true,chromeCreated:1,identity,previousFinished,appInitiallyClosed:true,delayedAppStartupMs:delay,launchSignaledMs:signaledAt-clickAt,appReadyMs:readyAt-clickAt,nativeDialogMs:promptedAt-clickAt,seconds:(Date.now()-clickAt)/1000,nativeRows:1,browserTransport:rows[0].browserTransfer,bytes:fs.statSync(rows[0].destination).size,sha256:sha(reference),chromeRetained:0,status:'Completed',scope:'Actual Chrome, production controller and Native Messaging host with test-only launch signal; hidden private app starts after delay, original single request reaches native save dialog and completes, independent reference hash. Not a physical PC reboot.'};fs.writeFileSync(path.join(out,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
}catch(error){if(context){try{const worker=context.serviceWorkers()[0];fs.writeFileSync(path.join(out,'failure.json'),JSON.stringify(await worker.evaluate(async()=>({state:await chrome.storage.local.get('handoffState'),items:await chrome.downloads.search({})})),null,2));}catch{}}throw error;
}finally{if(timer)clearInterval(timer);if(context)await context.close();if(starting)await starting;if(service&&service.exitCode===null){service.stdin.write('exit\n');await new Promise(resolve=>{service.once('exit',resolve);setTimeout(resolve,5000);});if(service.exitCode===null)service.kill();}if(fixture)fixture.close();cp.spawnSync(python,[registry,'restore',path.join(root,'registry.json'),'--isolated'],{encoding:'utf8'});}})().catch(error=>{console.error(error);process.exitCode=1;});
