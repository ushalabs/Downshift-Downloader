// Only the current player's media descriptors cross the bridge. No cookies,
// authorization, player code, cipher programs or arbitrary helper arguments.
export function youtubeStreamUrl(value, manifest=false) {
  try {
    if(typeof value!=="string" || value.length>8192 || /[\x00-\x20\x7f]/.test(value))return null;
    const u=new URL(value);
    if(u.protocol!=="https:" || u.username || u.password || u.port || !/(^|\.)googlevideo\.com$/.test(u.hostname))return null;
    if(manifest)return /\/api\/manifest\/(?:hls(?:_variant|_playlist)?|dash)\//.test(u.pathname)?u.href:null;
    if(u.pathname!=="/videoplayback" || u.searchParams.get('ump')==='1' || u.searchParams.has('sabr'))return null;
    // Byte ranges are transport hints, never remove a signed parameter.
    const signed=[u.searchParams.get('sparams'),u.searchParams.get('lsparams')].join(',').split(',');
    if(u.searchParams.has('range')){if(signed.includes('range'))return null;u.searchParams.delete('range');}
    if(u.searchParams.has('expire') && Number(u.searchParams.get('expire'))*1000<Date.now()+30000)return null;
    return u.href;
  }catch{return null;}
}
export function youtubeDescriptor(value, videoId) {
  if(!value || value.id!==videoId || !/^[\w-]{11}$/.test(videoId) || !Number.isFinite(value.duration) || value.duration<=0 || value.duration>604800)return null;
  const result={id:videoId,duration:value.duration,formats:[]};
  const manifest=youtubeStreamUrl(value.manifest,true);if(manifest)result.manifest=manifest;
  for(const f of (Array.isArray(value.formats)?value.formats:[]).slice(0,64)){
    const url=youtubeStreamUrl(f.url),mime=String(f.mime||'');
    if(!url || !/^(?:video|audio)\/(?:mp4|webm)$/.test(mime) || !Number.isInteger(f.itag) || f.itag<1 || f.itag>10000 || !Number.isSafeInteger(f.bytes) || f.bytes<=0)continue;
    const video=mime.startsWith('video/');
    if(video && (!Number.isInteger(f.height) || f.height<1 || f.height>16384))continue;
    const format={url,format_id:String(f.itag),ext:mime.endsWith('webm')?'webm':video?'mp4':'m4a',vcodec:video?'unknown':'none',acodec:video?(f.audio===true?'unknown':'none'):'unknown',filesize:f.bytes};
    if(video)format.height=f.height;
    if(result.formats.some(x=>x.format_id===format.format_id))continue;
    result.formats.push(format);
    if(result.formats.length>12 || JSON.stringify(result).length>38000){result.formats.pop();break;}
  }
  const audio=result.formats.some(f=>f.vcodec==='none'||f.acodec!=='none');
  if(!audio)result.formats=[];
  return result.manifest || result.formats.some(f=>f.vcodec!=='none') ? result:null;
}
