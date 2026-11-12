import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import {spawnSync} from 'node:child_process';
import {fileURLToPath} from 'node:url';
const repo=path.resolve(path.dirname(fileURLToPath(import.meta.url)),'..');

if(process.argv[2]==='--api-case'){
  const firefox=process.argv[3]==='firefox';
  const events=new Map();let imageMenus=0;
  const event=name=>({addListener:(listener,...options)=>events.set(name,{listener,options})});
  const api={
    runtime:{id:firefox?'downshift@ushalabs':'empnpdkfnffpcgnfgdndkbielplcoioe',getManifest:()=>({version:'1.0.0'}),
      ...(firefox?{getBrowserInfo:async()=>({name:'Firefox'})}:{}),onInstalled:event('installed'),onStartup:event('startup'),onMessage:event('message')},
    webRequest:{onBeforeRequest:event('request'),onBeforeSendHeaders:event('headers'),onHeadersReceived:event('response')},
    tabs:{onRemoved:event('tab-removed')},
    storage:{local:{get:async()=>({handoffState:{enabled:false,pending:{},bypass:[],finished:[]}}),set:async()=>{}},onChanged:event('storage')},
    alarms:{create:()=>{},onAlarm:event('alarm')},
    downloads:{search:async()=>[],onCreated:event('created'),onChanged:event('changed'),
      ...(!firefox?{onDeterminingFilename:event('filename'),setUiOptions:async()=>{}}:{})},
    contextMenus:{removeAll:async(...args)=>assert.equal(args.length,0),create:(options,callback)=>{assert.equal(options.id,'downshift-save-image');imageMenus++;callback();},onClicked:event('image')}
  };
  if(firefox)globalThis.browser=api;else globalThis.chrome=api;
  globalThis.setInterval=()=>0;
  await import('../extension/background.js');
  await new Promise(resolve=>setTimeout(resolve,20));
  assert.equal(imageMenus,1);
  assert.deepEqual(events.get('headers').options[1],firefox?['requestHeaders']:['requestHeaders','extraHeaders']);
  assert.equal(events.has('filename'),!firefox);
  assert(events.has('message')&&events.has('created')&&events.has('changed'));
  const {downloadItem,FirefoxDownloads}=await import('../extension/platform.js');
  const stamp=new Date('2026-11-12T01:02:03Z');
  assert.equal(downloadItem({id:1,startTime:stamp}).startTime,stamp.toISOString());
  assert.equal(downloadItem({id:1,startTime:'unchanged'}).startTime,'unchanged');
  if(firefox){
    const memory={};
    const storage={get:async key=>({[key]:structuredClone(memory[key])}),set:async values=>Object.assign(memory,structuredClone(values))};
    const first={id:4,startTime:stamp.toISOString(),state:'in_progress',filename:'C:/Private/secret.bin',url:'https://example.test/file?private-token=secret'};
    let items=[first];
    let pauses=0;
    const downloads={search:async()=>items,pause:async()=>{pauses++;}};
    const identities=new FirefoxDownloads(storage,downloads);
    const created=await identities.normalize(first,true);
    const resumed={...first,startTime:new Date(stamp.getTime()+60000).toISOString()};
    assert.equal((await identities.normalize(resumed)).startTime,created.startTime,'Firefox resume retains its original receipt identity');
    const held=await identities.normalize({...resumed,state:'interrupted',paused:true,canResume:true,error:'USER_CANCELED'});
    assert.equal(held.state,'in_progress');assert.equal(held.error,undefined);
    await identities.pause(4);assert.equal(pauses,0,'A zero-byte Firefox response is not cancelled by an unsafe early pause');
    items=[{...first,bytesReceived:65536}];
    await identities.pause(4);assert.equal(pauses,1,'Firefox can pause after retaining a safe prefix');
    const state={pending:{4:{startTime:created.startTime,firefoxIdentity:identities.live.get(4).identity,key:'native-receipt'}}};
    items=[{...resumed,id:9}];
    const reopened=new FirefoxDownloads(storage,downloads);
    const recovered=await reopened.restore(state);
    assert.equal(recovered.pending[9].key,'native-receipt','Browser restart remaps Firefox IDs without creating another native transfer');
    assert.equal(recovered.pending[4],undefined);
    const next={...resumed,id:9,startTime:new Date(stamp.getTime()+120000).toISOString()};
    assert.equal((await reopened.normalize(next,true)).startTime,next.startTime,'A new creation can reuse an erased Firefox ID');
    assert(!JSON.stringify(memory).includes('secret'),'Identity storage contains no source URLs or filenames');
  }
  console.log(`${process.argv[3]} background compatibility passed`);
}else{
  const temporary=fs.mkdtempSync(path.join(os.tmpdir(),'downshift-browser-manifests-'));
  try{
    const output=path.join(temporary,'extensions');
    const build=spawnSync('powershell.exe',['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File',path.join(repo,'scripts','Build-BrowserExtensions.ps1'),'-OutputDirectory',output],{encoding:'utf8'});
    assert.equal(build.status,0,build.stdout+build.stderr);
    for(const target of ['edge','firefox','opera']){
      const folder=path.join(output,target),manifest=JSON.parse(fs.readFileSync(path.join(folder,'manifest.json')));
      assert.equal(manifest.version,'1.0.0');assert.equal(manifest.manifest_version,3);
      assert(manifest.permissions.includes('nativeMessaging')&&manifest.permissions.includes('downloads'));
      for(const file of ['background.js','platform.js','popup.html','video-button.js','media-probe.js'])assert(fs.existsSync(path.join(folder,file)));
      if(target==='firefox'){
        assert.equal(manifest.key,undefined);assert.equal(manifest.minimum_chrome_version,undefined);
        assert(!manifest.permissions.includes('downloads.ui'));
        assert.deepEqual(manifest.background,{scripts:['background.js'],type:'module'});
        assert.equal(manifest.browser_specific_settings.gecko.id,'downshift@ushalabs');
        assert.equal(manifest.browser_specific_settings.gecko.strict_min_version,'140.0');
        assert.equal(manifest.incognito,'not_allowed');
        assert.deepEqual(manifest.browser_specific_settings.gecko.data_collection_permissions.required,['browsingActivity','websiteContent','websiteActivity']);
      }else{assert(manifest.key);assert.equal(manifest.background.service_worker,'background.js');}
      const worker=spawnSync(process.execPath,[fileURLToPath(import.meta.url),'--api-case',target],{encoding:'utf8'});
      assert.equal(worker.status,0,worker.stdout+worker.stderr);process.stdout.write(worker.stdout);
    }
    console.log('Three browser manifests, shared payloads, native identities and API compatibility verified.');
  }finally{fs.rmSync(temporary,{recursive:true,force:true});}
}
