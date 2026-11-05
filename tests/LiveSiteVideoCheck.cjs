// Explicit public-site acceptance; private profile, queue and native host only.
const {createRequire}=require('node:module'),fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),readline=require('node:readline'),assert=require('node:assert/strict'),crypto=require('node:crypto');
const requireTools=createRequire(path.join(process.env.DOWNSHIFT_NODE_MODULES,'package.json')),{chromium}=requireTools('playwright');
const [chrome,driver,hostInput,python,outInput]=process.argv.slice(2,7).map(x=>path.resolve(x)),target=new URL(process.argv[7]),height=Number(process.argv[8]||720),repo=path.resolve(__dirname,'..'),out=outInput;
assert(target.protocol==='https:'&&target.hostname==='www.loom.com'&&/^\/share\/[a-f\d]{32}$/.test(target.pathname));
fs.mkdirSync(out,{recursive:true});const root=fs.mkdtempSync(path.join(out,'run-')),native=fs.mkdtempSync(path.join(require('node:os').tmpdir(),'ds-site-'));
const {host,extension}=require('./IsolatedBrowser.cjs').isolatedBrowser(repo,root,hostInput),registry=path.join(repo,'tests/ChromeRegistry.py');let service,context;
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
async function until(test,timeout=120000){const end=Date.now()+timeout;while(Date.now()<end){if(await test())return;await new Promise(resolve=>setTimeout(resolve,200));}throw Error('Live site acceptance timed out');}
(async()=>{try{
 const registered=cp.spawnSync(python,[registry,'install',host,path.join(root,'host.json'),path.join(root,'registry.json'),'--isolated'],{encoding:'utf8'});assert.equal(registered.status,0,registered.stderr);
 service=cp.spawn(driver,[native,'--isolated-browser-test'],{stdio:['pipe','pipe','pipe']});service.stderr.on('data',data=>process.stderr.write(data));
 const lines=[],waiting=[];readline.createInterface({input:service.stdout}).on('line',line=>waiting.length?waiting.shift()(line):lines.push(line));
 async function read(){if(lines.length)return lines.shift();return new Promise((resolve,reject)=>{const timer=setTimeout(()=>reject(Error('Private driver timed out')),30000);waiting.push(line=>{clearTimeout(timer);resolve(line);});});}
 assert.equal(await read(),'ready');async function command(value){service.stdin.write(value+'\n');return JSON.parse(await read());}
 context=await chromium.launchPersistentContext(path.join(root,'profile'),{executablePath:chrome,headless:true,ignoreDefaultArgs:['--disable-extensions'],args:[`--disable-extensions-except=${extension}`,`--load-extension=${extension}`,'--no-first-run']});
 const page=await context.newPage();await page.goto(target.href,{waitUntil:'domcontentloaded',timeout:60000});
 const overlay=page.locator('[data-downshift-video]:visible').first();await overlay.waitFor({state:'visible',timeout:45000});await overlay.click({position:{x:65,y:18}});
 let task;await until(async()=>{task=(await command('media'))[0];return task?.ready;});assert.equal(task.button,'Download video',task.status);assert(task.qualities.includes(height+'p'),task.qualities);console.log('Live Loom qualities:',task.qualities.join(', '));
 await command('media-height '+height);await command('media-download');await until(async()=>{task=(await command('media'))[0];if(task?.ready&&task.rate!=='Complete')throw Error(task.status);return task?.rate==='Complete';},600000);
 const rows=await command('snapshot');assert.equal(rows.length,1);assert.equal(rows[0].status,3);assert.equal(rows[0].url,target.href);const file=rows[0].destination;
 const probe=cp.spawnSync(path.join(path.dirname(host),'media-tools/ffprobe.exe'),['-v','error','-show_streams','-show_format','-of','json',file],{encoding:'utf8',timeout:30000});assert.equal(probe.status,0,probe.stderr);const info=JSON.parse(probe.stdout);
 assert(info.streams.some(s=>s.codec_type==='video'&&s.height===height));if(process.argv[9]==='video-only')assert(!info.streams.some(s=>s.codec_type==='audio'));else assert(info.streams.some(s=>s.codec_type==='audio'));assert(Number(info.format.duration)>0);
 const sources=['extension/app-launcher.js','extension/background.js','extension/handoff.js','extension/file-links.js','extension/media.js','extension/video-button.js','tests/LiveSiteVideoCheck.cjs'];
 const report={url:target.href,selected:height+'p',qualities:task.qualities,bytes:fs.statSync(file).size,sha256:sha(file),duration:Number(info.format.duration),streams:info.streams.map(s=>({type:s.codec_type,codec:s.codec_name,height:s.height})),status:'Completed',chromeVersion:context.browser().version(),normalProfileOrRegistrationUsed:false,sources:Object.fromEntries(sources.map(file=>[file,sha(path.join(repo,file))]))};fs.writeFileSync(path.join(out,'report.json'),JSON.stringify(report,null,2));console.log('Full public Loom video verified:',report.bytes,'bytes;',report.duration,'seconds.');
}finally{
 if(context)await context.close();if(service&&service.exitCode===null){service.stdin.write('exit\n');await new Promise(resolve=>{service.once('exit',resolve);setTimeout(resolve,5000);});if(service.exitCode===null)service.kill();}
 const restored=cp.spawnSync(python,[registry,'restore',path.join(root,'registry.json'),'--isolated'],{encoding:'utf8'});assert.equal(restored.status,0,restored.stderr);
}})().catch(error=>{console.error(error);process.exitCode=1;});
