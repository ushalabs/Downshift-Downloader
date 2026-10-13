// Explicit opt-in Internet acceptance check; deliberately absent from CTest.
const {createRequire}=require('node:module');
const requireTools=createRequire(require('node:path').join(process.env.DOWNSHIFT_NODE_MODULES,'package.json'));
const {chromium}=requireTools('playwright');
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict'),readline=require('node:readline'),crypto=require('node:crypto');
const [chrome,driver,hostInput,python,output]=process.argv.slice(2).map(x=>path.resolve(x));
const repo=path.resolve(__dirname,'..'),id='empnpdkfnffpcgnfgdndkbielplcoioe',videoId='HyfC9nZvIJM';
fs.mkdirSync(output,{recursive:true});const root=fs.mkdtempSync(path.join(output,'run-')),native=path.join(root,'native');fs.mkdirSync(native);
const {host,extension}=require('./IsolatedBrowser.cjs').isolatedBrowser(repo,root,hostInput);
const registry=path.join(repo,'tests','ChromeRegistry.py');let service,context;
const until=async(test,timeout=120000)=>{const end=Date.now()+timeout;while(Date.now()<end){if(await test())return;await new Promise(r=>setTimeout(r,250));}throw Error('Live YouTube check timed out');};
(async()=>{try{
  const installed=cp.spawnSync(python,[registry,'install',host,path.join(root,'host.json'),path.join(root,'registry.json'),'--isolated'],{encoding:'utf8'});assert.equal(installed.status,0,installed.stderr);
  service=cp.spawn(driver,[native,'--isolated-browser-test'],{stdio:['pipe','pipe','pipe']});service.stderr.on('data',data=>process.stderr.write(data));
  const lines=[],waiting=[];let stopped;
  readline.createInterface({input:service.stdout}).on('line',line=>waiting.length?waiting.shift().resolve(line):lines.push(line));
  service.on('exit',()=>{stopped=Error('Native test driver exited');for(const waiter of waiting.splice(0))waiter.reject(stopped);});
  const read=()=>lines.length?Promise.resolve(lines.shift()):stopped?Promise.reject(stopped):new Promise((resolve,reject)=>waiting.push({resolve,reject}));
  assert.equal(await read(),'ready');
  const command=async value=>{service.stdin.write(value+'\n');return JSON.parse(await read());};
  context=await chromium.launchPersistentContext(path.join(root,'profile'),{executablePath:chrome,headless:true,ignoreDefaultArgs:['--disable-extensions'],args:[`--disable-extensions-except=${extension}`,`--load-extension=${extension}`,'--no-first-run']});
  const page=await context.newPage();const searchReady=await page.goto('https://www.youtube.com/results?search_query=The+100+That+Changed+Pakistan+T20+Cricket',{waitUntil:'domcontentloaded',timeout:60000}).then(()=>true).catch(()=>false);
  const link=page.locator(`a#video-title[href*="${videoId}"]`).first();let spa=false;
  if(searchReady && await link.waitFor({state:'visible',timeout:15000}).then(()=>true).catch(()=>false)){
    await page.evaluate(()=>globalThis.__downshiftDocumentProbe='initial-search');await link.click();await page.waitForURL(`**/watch?v=${videoId}*`,{timeout:30000});
    spa=await page.evaluate(()=>globalThis.__downshiftDocumentProbe==='initial-search');
  }else await page.goto(`https://www.youtube.com/watch?v=${videoId}`,{waitUntil:'domcontentloaded',timeout:60000});
  await until(async()=>await page.locator('[data-downshift-video]').first().isVisible());
  // A malformed player title link must not override the valid watch page.
  const malformedPlayerTitle=await page.evaluate(()=>{const player=document.querySelector('.html5-video-player');if(!player)return false;let link=player.querySelector('a.ytp-title-link');if(!link){link=document.createElement('a');link.className='ytp-title-link';player.append(link);}link.href='https://www.youtube.com/watch?v=undefined';return true;});assert(malformedPlayerTitle);
  await page.screenshot({path:path.join(output,'youtube-player.png')});
  await page.locator('[data-downshift-video]').first().click({position:{x:65,y:17}});
  await until(async()=>{const tasks=await command('media');return tasks[0]?.ready;});
  const task=(await command('media'))[0];assert.equal(task.button,'Download video',task.status);assert(task.qualities.includes('1080p'),task.qualities);assert(task.qualities.includes('144p'),task.qualities);assert(task.qualities.length>3);
  await command('media-height 144');assert.equal((await command('media'))[0].selected,'144p');
  await command('media-shot '+path.join(output,'youtube-qualities.bmp'));
  console.log('YouTube qualities:',task.qualities.join(', '),'SPA:',spa);
  await command('media-download');let final;
  await until(async()=>{final=(await command('media'))[0];if(final?.ready && final.rate!=='Complete')throw Error(final.status);return final?.rate==='Complete';},600000);
  const rows=await command('snapshot');assert.equal(rows.length,1);assert.equal(rows[0].media,true);assert.equal(rows[0].status,3);
  const file=rows[0].destination;assert(fs.statSync(file).size>1000000);
  const probe=cp.spawnSync(path.join(path.dirname(host),'media-tools','ffprobe.exe'),['-v','error','-show_streams','-show_format','-of','json',file],{encoding:'utf8',timeout:30000});assert.equal(probe.status,0,probe.stderr);
  const info=JSON.parse(probe.stdout);assert(info.streams.some(s=>s.codec_type==='video' && s.height<=144));assert(info.streams.some(s=>s.codec_type==='audio'));assert(Math.abs(Number(info.format.duration)-713)<3);
  const digest=crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const fingerprint=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
  const sources=['extension/media.js','extension/video-button.js','src/gui/MediaDialog.cpp','src/gui/MainWindow.cpp','src/gui/Dialogs.cpp','src/core/DownloadQueue/QueueManager.cpp','src/browser/PipeServer.cpp','src/core/DownloadEngine/DownloadEngine.cpp','src/core/ConnectionManager/AdaptiveController.h','tests/IsolatedBrowser.cjs','tests/LiveYouTubeCheck.cjs'];
  const report={url:`https://www.youtube.com/watch?v=${videoId}`,chromeVersion:context.browser().version(),sameDocumentNavigation:spa,malformedPlayerTitle,extensionVersion:JSON.parse(fs.readFileSync(path.join(repo,"extension/manifest.json"))).version,qualities:task.qualities,selected:'144p',file,bytes:fs.statSync(file).size,sha256:digest,duration:Number(info.format.duration),streams:info.streams.map(s=>({codec_type:s.codec_type,codec_name:s.codec_name,width:s.width,height:s.height})),historyStatus:'Completed',nativeBridge:'separate test host and pipe; normal registration untouched',nativeDriverSha256:fingerprint(driver),sources:Object.fromEntries(sources.map(file=>[file,fingerprint(path.join(repo,file))])),scope:'Real YouTube page, trusted extension-player click, actual native dialog, full video+audio transfer, merge, verified persisted completion and independent ffprobe/hash'};
  fs.writeFileSync(path.join(output,'report.json'),JSON.stringify(report,null,2));await command('media-shot '+path.join(output,'youtube-completed.bmp'));
  console.log('Full YouTube download verified:',file,'bytes:',report.bytes,'duration:',report.duration);
}finally{
  if(context)await context.close();if(service && service.exitCode===null){service.stdin.write('exit\n');await new Promise(resolve=>{service.once('exit',resolve);setTimeout(resolve,5000);});if(service.exitCode===null)service.kill();}
  const restored=cp.spawnSync(python,[registry,'restore',path.join(root,'registry.json'),'--isolated'],{encoding:'utf8'});assert.equal(restored.status,0,restored.stderr);
}})().catch(error=>{console.error(error);process.exitCode=1;});
