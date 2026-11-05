// Real isolated Chrome/MV3/world/gesture/request-context acceptance. Native
// packets terminate at this private worker; MediaTests verifies their transfer.
const {createRequire}=require('node:module'),fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict'),crypto=require('node:crypto'),https=require('node:https');
const requireTools=createRequire(path.join(process.env.DOWNSHIFT_NODE_MODULES,'package.json'));
const {chromium}=requireTools('playwright');
const [chrome,out]=process.argv.slice(2).map(x=>path.resolve(x)),repo=path.resolve(__dirname,'..');
(async()=>{
 fs.mkdirSync(out,{recursive:true});const root=fs.mkdtempSync(path.join(out,'run-')),extension=path.join(root,'extension');fs.cpSync(path.join(repo,'extension'),extension,{recursive:true});
 const worker=path.join(extension,'background.js'),original="const connection=new NativeConnection(()=>chrome.runtime.connectNative('org.ushalabs.downshift'));";
 const source=fs.readFileSync(worker,'utf8');assert(source.includes(original));
 fs.writeFileSync(worker,source.replace(original,"globalThis.__packets=[];globalThis.__nativeCalls=[];globalThis.__sourceState=()=>({observed:[...mediaSources.youtubeMedia.values()],requests:[...mediaSources.requests.values()]});const connection={request:async request=>{globalThis.__nativeCalls.push({command:request.command,at:Date.now()});if(globalThis.__coldUntil>Date.now()){if(request.command==='open')return {version:1,ok:true,state:'opening'};if(request.command==='ping')return {version:1,ok:false,error:'Fixture app still starting'};}if(request.command==='media')globalThis.__packets.push(request);return {version:1,ok:true,state:'ready',appVersion:chrome.runtime.getManifest().version,capabilities:{mediaStreams:true,browserYouTube:true}};}};"));
 const button=path.join(extension,'video-button.js');fs.writeFileSync(button,fs.readFileSync(button,'utf8').replace('attachShadow({mode:"closed"})','attachShadow({mode:"open"})'));
 // Real HTTPS responses are necessary here: debugger route.fulfill does not
 // emit Chrome webRequest events, so it cannot verify stream observation.
 const cert=fs.readFileSync(path.join(repo,'tests/fixtures/localhost-cert.pem')),key=fs.readFileSync(path.join(repo,'tests/fixtures/localhost-key.pem'));
 let handler;
 const server=https.createServer({cert,key},async(req,res)=>{
  if(req.headers.host==='rr1.googlevideo.com'){const audio=req.url.includes('itag=140');res.writeHead(200,{'Content-Type':audio?'audio/mp4':'video/mp4','Access-Control-Allow-Origin':'https://www.youtube.com','Cache-Control':'no-store'});res.end(Buffer.alloc(audio?30:100));return;}
  let body='';for await(const chunk of req){body+=chunk;if(body.length>32768){res.destroy();return;}}
  try{await handler({request:()=>({url:()=>`https://${req.headers.host}${req.url}`,postDataJSON:()=>JSON.parse(body),allHeaders:async()=>req.headers,resourceType:()=>['document','iframe'].includes(req.headers['sec-fetch-dest'])?'document':'script'}),fulfill:async value=>{res.writeHead(value.status||200,{'Content-Type':value.contentType||'text/plain','Cache-Control':'no-store'});res.end(value.body);},abort:async()=>res.end()});}catch(error){res.destroy();console.error(error);}
 });
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));server.unref();
 const pin=crypto.createHash('sha256').update(new crypto.X509Certificate(cert).publicKey.export({type:'spki',format:'der'})).digest('base64');
 const context=await chromium.launchPersistentContext(path.join(root,'profile'),{executablePath:chrome,headless:true,ignoreDefaultArgs:['--disable-extensions'],args:[`--disable-extensions-except=${extension}`,`--load-extension=${extension}`,'--no-first-run','--no-proxy-server',`--host-resolver-rules=MAP rr1.googlevideo.com 127.0.0.1:${server.address().port},MAP www.youtube.com 127.0.0.1:${server.address().port},MAP www.linkedin.com 127.0.0.1:${server.address().port},MAP www.loom.com 127.0.0.1:${server.address().port}`,`--ignore-certificate-errors-spki-list=${pin}`]});
 try{
  await context.addCookies([{name:'fixture-session',value:'kept-in-browser',domain:'www.youtube.com',path:'/',secure:true,httpOnly:true,sameSite:'Lax'},{name:'SAPISID',value:'synthetic-not-a-real-session',domain:'www.youtube.com',path:'/',secure:true,httpOnly:false,sameSite:'Lax'}]);
  let apiCalls=0,mode='watch',checks=0;const id='FQmqRVK3kHg',manifest='https://manifest.googlevideo.com/api/manifest/hls_variant/id/fixture/index.m3u8';
  const videoUrl='https://rr1.googlevideo.com/videoplayback?id=fixture&itag=137&clen=100&n=loaded',audioUrl='https://rr1.googlevideo.com/videoplayback?id=fixture&itag=140&clen=30';
  handler=async route=>{
   if(mode==='opaque' && route.request().resourceType()==='document'){
    const iframe=route.request().url().endsWith('/opaque-player');
    await route.fulfill({contentType:'text/html',body:iframe
      ? `<!doctype html><video style="width:320px;height:180px"></video><script>addEventListener('message',event=>{if(event.source===window&&event.data?.type==='downshift-manifest-replay')document.body.dataset.replayOrigin=event.origin;});</script>`
      : '<!doctype html><iframe sandbox="allow-scripts" src="/opaque-player" width="400" height="240"></iframe>'});
    return;
   }
   if(['thumbnails','linkedin','loom'].includes(mode) && route.request().resourceType()==='document'){
    const card=(videoId)=>`<ytd-rich-item-renderer style="display:block;width:360px;height:240px"><yt-lockup-view-model><a id="thumbnail" style="display:block;width:320px;height:180px;background:#ddd" href="/watch?v=${videoId}">Video thumbnail</a></yt-lockup-view-model></ytd-rich-item-renderer>`;
    const linkedinVideo=(number)=>`<article data-urn="urn:li:activity:${number}"><video style="width:320px;height:180px" data-sources='[{"src":"https://cdn.example/${number}-small.mp4","height":144},{"src":"https://cdn.example/${number}-large.mp4","height":720}]'></video></article>`;
    await route.fulfill({contentType:'text/html',body:mode==='thumbnails'?`<!doctype html><title>Thumbnails</title>${card(id)}${card('HyfC9nZvIJM')}<script>for(const a of document.querySelectorAll('a#thumbnail')){a.addEventListener('mouseenter',()=>{const v=document.createElement('video');v.style.cssText='width:320px;height:180px';a.append(v);});a.addEventListener('mouseleave',()=>{for(const v of a.querySelectorAll('video'))v.remove();});}</script>`:mode==='linkedin'?`<!doctype html><title>LinkedIn feed fixture</title>${linkedinVideo('1111111111111')}${linkedinVideo('2222222222222')}`:'<!doctype html><title>Loom fixture</title><video style="width:640px;height:360px"></video><video style="display:none"></video><video style="display:none"></video>'});return;
   }
   if(route.request().url().includes('/youtubei/v1/player')){
    apiCalls++;const request=route.request(),body=request.postDataJSON(),headers=await request.allHeaders();assert.equal(body.videoId,id);assert(body.context.client.userAgent.includes('Safari'));assert(headers.cookie.includes('fixture-session=kept-in-browser'));assert(headers.authorization.startsWith('SAPISIDHASH '));assert.equal(headers['x-origin'],'https://www.youtube.com');
    const [timestamp,signature]=headers.authorization.slice('SAPISIDHASH '.length).split('_');assert.equal(signature,crypto.createHash('sha1').update(timestamp+' synthetic-not-a-real-session https://www.youtube.com').digest('hex'));assert.equal(body.playbackContext.contentPlaybackContext.signatureTimestamp,12345);
    if(mode==='refused'||mode==='observed'){await route.fulfill({status:403,body:'Fixture refusal'});return;}
    await route.fulfill({status:200,contentType:'application/json',body:JSON.stringify({playabilityStatus:{status:'OK'},videoDetails:{videoId:id,lengthSeconds:'2'},streamingData:{hlsManifestUrl:manifest}})});return;
   }
   if(route.request().url().includes('/s/player/fixture/player_ias.vflset/en_US/base.js')){await route.fulfill({status:200,contentType:'text/javascript',body:'window.fixturePlayer={signatureTimestamp:12345};'});return;}
   if(route.request().resourceType()!=='document'){await route.abort();return;}
   const response={playabilityStatus:{status:'OK'},videoDetails:{videoId:id,lengthSeconds:'2'},streamingData:{serverAbrStreamingUrl:'https://rr.googlevideo.com/sabr'}};
   if(mode==='feed'||mode==='observed')response.streamingData={adaptiveFormats:[
    // Same-height unavailable WebM must not hide the browser's loaded MP4.
    {itag:248,mimeType:'video/webm',height:1080,contentLength:'100',signatureCipher:'url='+encodeURIComponent(videoUrl.replace('itag=137','itag=248'))},
    {itag:137,mimeType:'video/mp4',height:1080,contentLength:'100',url:videoUrl.replace('n=loaded','n=untransformed')},
    {itag:140,mimeType:'audio/mp4',contentLength:'30',url:audioUrl}]};
   if(mode==='muxed')response.streamingData={formats:[{itag:18,mimeType:'video/mp4',height:360,contentLength:'100',audioQuality:'AUDIO_QUALITY_LOW',url:videoUrl.replace('itag=137','itag=18')}],adaptiveFormats:[{itag:137,mimeType:'video/mp4',height:1080,contentLength:'100',signatureCipher:'url='+encodeURIComponent(videoUrl)}]};
   await route.fulfill({status:200,contentType:'text/html',body:`<!doctype html><title>(5) YouTube</title><div id="movie_player" class="html5-video-player"><video style="width:640px;height:360px"></video></div><script>const player=document.getElementById('movie_player');player.getVideoData=()=>({video_id:'${id}'});player.getPlayerResponse=()=>${mode==='metadata'?'({})':mode==='feed'?JSON.stringify(JSON.stringify(response)):'('+JSON.stringify(response)+')'};Object.defineProperty(player.querySelector('video'),'duration',{value:2});window.ytcfg={get:key=>key==='INNERTUBE_CONTEXT'?{client:{clientVersion:'fixture',visitorData:'only-browser'}}:undefined};${mode==='observed'?`Promise.all([fetch('${videoUrl}'),fetch('${audioUrl}')]).then(()=>window.loadedFixture=true,e=>window.loadedFixture=String(e));`:''}</script><script src="/s/player/fixture/player_ias.vflset/en_US/base.js"></script>`});
  };
  const page=await context.newPage(),service=context.serviceWorkers()[0]||await context.waitForEvent('serviceworker');
  page.on('requestfailed',request=>{if(new URL(request.url()).hostname==='rr1.googlevideo.com')console.error('Fixture request failed: '+request.failure()?.errorText);});
  for(const scenario of ['watch','feed','metadata','muxed','observed','refused']){
   mode=scenario;await service.evaluate(()=>globalThis.__packets=[]);const before=apiCalls;
   await page.goto(mode==='watch'?'https://www.youtube.com/watch?v='+id:'https://www.youtube.com/?fixture='+mode);
   if(mode==='observed'){await page.waitForFunction(()=>window.loadedFixture!==undefined);assert.equal(await page.evaluate(()=>window.loadedFixture),true);}
   await page.locator('[data-downshift-video] button').click({timeout:20000});
   await page.waitForFunction(()=>{const notice=document.querySelector('[data-downshift-video]')?.shadowRoot?.querySelector('span')?.textContent;return notice==='Choose quality in Downshift.'||notice?.includes('cannot download');},{},{timeout:20000});
   const packets=await service.evaluate(()=>globalThis.__packets);
   if(mode==='refused'){assert.equal(packets.length,1,'Missing browser streams must not block the working native YouTube extractor');assert.equal(packets[0].url,'https://www.youtube.com/watch?v='+id);assert(!packets[0].context.youtube);assert.equal(apiCalls-before,1);checks+=4;continue;}
   assert.equal(packets.length,1);assert.equal(packets[0].url,'https://www.youtube.com/watch?v='+id);assert.equal(packets[0].context.youtube.duration,2);checks+=3;
   if(mode==='watch'||mode==='metadata'||mode==='muxed'){assert.equal(apiCalls-before,1);assert.equal(packets[0].context.youtube.manifest,manifest);checks+=2;}
   else {assert.equal(apiCalls-before,0);assert(packets[0].context.youtube.formats.some(f=>f.format_id==='137'));assert(packets[0].context.youtube.formats.some(f=>f.format_id==='140'));checks+=3;}
   if(mode==='observed'){assert.equal(packets[0].context.youtube.formats.find(f=>f.format_id==='137').url,videoUrl,JSON.stringify(await service.evaluate(()=>globalThis.__sourceState())));checks++;}
   for(const secret of ['kept-in-browser','synthetic-not-a-real-session','SAPISIDHASH','only-browser']){assert(!JSON.stringify(packets).includes(secret));checks++;}
  }
  // A stale card/player mismatch must not fetch a different video.
  mode='feed';await service.evaluate(()=>globalThis.__packets=[]);await page.goto('https://www.youtube.com/watch?v=HyfC9nZvIJM');const before=apiCalls;
  await page.locator('[data-downshift-video] button').click();await page.waitForFunction(()=>document.querySelector('[data-downshift-video]')?.shadowRoot?.querySelector('span')?.textContent==='Choose quality in Downshift.');
  assert.equal(apiCalls,before);const nativeOnly=await service.evaluate(()=>globalThis.__packets);assert.equal(nativeOnly.length,1);assert.equal(nativeOnly[0].url,'https://www.youtube.com/watch?v=HyfC9nZvIJM');assert(!nativeOnly[0].context.youtube);checks+=4;
  mode='thumbnails';await service.evaluate(()=>globalThis.__packets=[]);await page.goto('https://www.youtube.com/?fixture=thumbnails');await page.waitForFunction(()=>document.querySelectorAll('[data-downshift-video]').length===2);
  const firstCard=page.locator('ytd-rich-item-renderer').first(),firstButton=page.locator('[data-downshift-video] button').first();await firstCard.locator('a#thumbnail').hover();await firstButton.hover();await page.waitForTimeout(250);assert(await firstButton.isVisible());assert.equal(await page.locator('[data-downshift-video]').count(),2);checks+=2;
  await firstButton.click();await page.waitForFunction(()=>globalThis.location.href.includes('fixture=thumbnails') && document.querySelector('[data-downshift-video]')?.shadowRoot.querySelector('span')?.textContent==='Choose quality in Downshift.');assert.equal((await service.evaluate(()=>globalThis.__packets))[0].url,'https://www.youtube.com/watch?v='+id);checks++;
  await page.locator('[data-downshift-video] button').nth(1).click();await page.waitForTimeout(100);assert.equal((await service.evaluate(()=>globalThis.__packets)).at(-1).url,'https://www.youtube.com/watch?v=HyfC9nZvIJM');checks++;
  mode='linkedin';await service.evaluate(()=>globalThis.__packets=[]);await page.goto('https://www.linkedin.com/feed/');await page.waitForFunction(()=>document.querySelectorAll('[data-downshift-video]').length===2);await page.locator('[data-downshift-video] button').nth(1).click();await page.waitForTimeout(100);const linked=(await service.evaluate(()=>globalThis.__packets))[0];assert.equal(linked.url,'https://cdn.example/2222222222222-large.mp4');assert.equal(linked.context.referer,'https://www.linkedin.com/feed/');assert(!JSON.stringify(linked).includes('1111111111111'));checks+=3;
  await page.evaluate(()=>{const post=document.querySelectorAll('article')[1];post.setAttribute('data-id',post.getAttribute('data-urn'));post.removeAttribute('data-urn');post.querySelector('video').removeAttribute('data-sources');});await page.locator('[data-downshift-video] button').nth(1).click();await page.waitForTimeout(100);assert.equal((await service.evaluate(()=>globalThis.__packets)).at(-1).url,'https://www.linkedin.com/feed/update/urn:li:activity:2222222222222');checks++;
  mode='loom';await service.evaluate(()=>globalThis.__packets=[]);await page.goto('https://www.loom.com/share/0123456789abcdef0123456789abcdef');await page.locator('[data-downshift-video]:visible button').click();await page.waitForTimeout(100);const loom=(await service.evaluate(()=>globalThis.__packets))[0];assert.equal(loom.url,'https://www.loom.com/share/0123456789abcdef0123456789abcdef');assert(!loom.context.detectedStream);checks+=2;
  mode='opaque';await page.goto('https://www.loom.com/opaque-fixture');
  const opaque=page.frameLocator('iframe');
  await opaque.locator('body[data-replay-origin="null"]').waitFor();
  assert(await opaque.locator('[data-downshift-video] button').isVisible());
  checks+=2;
  // A single trusted explicit-download click starts the control channel before
  // navigation/default handling; a slow startup settles without another click.
  await page.waitForTimeout(1600);await page.evaluate(()=>{const a=document.createElement('a');a.id='cold-download';a.href='https://www.loom.com/file.bin';a.download='file.bin';a.textContent='Download file';a.onclick=event=>event.preventDefault();document.body.prepend(a);});
  await service.evaluate(()=>{globalThis.__nativeCalls=[];globalThis.__coldUntil=Date.now()+12000;});const clickAt=Date.now();await page.locator('#cold-download').click();await page.waitForTimeout(100);assert((await service.evaluate(()=>globalThis.__nativeCalls)).some(call=>call.command==='open'&&call.at-clickAt<1500));checks++;
  await page.waitForTimeout(13000);const coldCalls=await service.evaluate(()=>globalThis.__nativeCalls);assert(coldCalls.filter(call=>call.command==='ping').length>8);assert(coldCalls.some(call=>call.command==='ping'&&call.at-clickAt>=11000));checks+=2;
  const report={checks,scope:'Controlled YouTube/LinkedIn/Loom origins in real private headless Chrome; optional browser descriptors, native extraction on missing browser streams, stable thumbnail overlays across preview removal, clicked multi-player feed source, canonical Loom page, trusted clicks and no session export. Full public transfers verified separately.',apiCalls,normalProfileOrRegistrationUsed:false,publicYouTubeDownloadVerified:false};
  fs.writeFileSync(path.join(out,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
 }finally{await context.close();server.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
