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
 fs.writeFileSync(worker,source.replace(original,"globalThis.__packets=[];globalThis.__sourceState=()=>({observed:[...mediaSources.youtubeMedia.values()],requests:[...mediaSources.requests.values()]});const connection={request:async request=>{if(request.command==='media')globalThis.__packets.push(request);return {version:1,ok:true,state:'ready',appVersion:chrome.runtime.getManifest().version,capabilities:{mediaStreams:true,browserYouTube:true}};}};"));
 const button=path.join(extension,'video-button.js');fs.writeFileSync(button,fs.readFileSync(button,'utf8').replace('attachShadow({mode:"closed"})','attachShadow({mode:"open"})'));
 // Real HTTPS responses are necessary here: debugger route.fulfill does not
 // emit Chrome webRequest events, so it cannot verify stream observation.
 const cert=fs.readFileSync(path.join(repo,'tests/fixtures/localhost-cert.pem')),key=fs.readFileSync(path.join(repo,'tests/fixtures/localhost-key.pem'));
 let handler;
 const server=https.createServer({cert,key},async(req,res)=>{
  if(req.headers.host==='rr1.googlevideo.com'){const audio=req.url.includes('itag=140');res.writeHead(200,{'Content-Type':audio?'audio/mp4':'video/mp4','Access-Control-Allow-Origin':'https://www.youtube.com','Cache-Control':'no-store'});res.end(Buffer.alloc(audio?30:100));return;}
  let body='';for await(const chunk of req){body+=chunk;if(body.length>32768){res.destroy();return;}}
  try{await handler({request:()=>({url:()=>`https://${req.headers.host}${req.url}`,postDataJSON:()=>JSON.parse(body),allHeaders:async()=>req.headers,resourceType:()=>req.headers['sec-fetch-dest']==='document'?'document':'script'}),fulfill:async value=>{res.writeHead(value.status||200,{'Content-Type':value.contentType||'text/plain','Cache-Control':'no-store'});res.end(value.body);},abort:async()=>res.end()});}catch(error){res.destroy();console.error(error);}
 });
 await new Promise(resolve=>server.listen(0,'127.0.0.1',resolve));server.unref();
 const pin=crypto.createHash('sha256').update(new crypto.X509Certificate(cert).publicKey.export({type:'spki',format:'der'})).digest('base64');
 const context=await chromium.launchPersistentContext(path.join(root,'profile'),{executablePath:chrome,headless:true,ignoreDefaultArgs:['--disable-extensions'],args:[`--disable-extensions-except=${extension}`,`--load-extension=${extension}`,'--no-first-run','--no-proxy-server',`--host-resolver-rules=MAP rr1.googlevideo.com 127.0.0.1:${server.address().port},MAP www.youtube.com 127.0.0.1:${server.address().port}`,`--ignore-certificate-errors-spki-list=${pin}`]});
 try{
  await context.addCookies([{name:'fixture-session',value:'kept-in-browser',domain:'www.youtube.com',path:'/',secure:true,httpOnly:true,sameSite:'Lax'},{name:'SAPISID',value:'synthetic-not-a-real-session',domain:'www.youtube.com',path:'/',secure:true,httpOnly:false,sameSite:'Lax'}]);
  let apiCalls=0,mode='watch',checks=0;const id='FQmqRVK3kHg',manifest='https://manifest.googlevideo.com/api/manifest/hls_variant/id/fixture/index.m3u8';
  const videoUrl='https://rr1.googlevideo.com/videoplayback?id=fixture&itag=137&clen=100&n=loaded',audioUrl='https://rr1.googlevideo.com/videoplayback?id=fixture&itag=140&clen=30';
  handler=async route=>{
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
  const report={checks,scope:'Controlled YouTube origin in real private headless Chrome; optional browser descriptors, native extraction remains available on missing/unusable browser streams, stale player cannot override canonical watch URL, trusted overlay clicks and no session export. Full public native transfer verified separately.',apiCalls,normalProfileOrRegistrationUsed:false,publicYouTubeDownloadVerified:false};
  fs.writeFileSync(path.join(out,'report.json'),JSON.stringify(report,null,2));console.log(JSON.stringify(report));
 }finally{await context.close();server.close();}
})().catch(error=>{console.error(error);process.exitCode=1;});
