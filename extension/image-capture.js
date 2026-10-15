import {safeFilename} from './handoff.js';
export async function saveImage(api,info,tab){
  if(tab?.incognito)throw Error('Private-window image capture is unavailable.');
  if(!await api.captureEnabled())throw Error('Enable Downshift capture to save this image.');
  const url=String(info.srcUrl||'');
  if(!/^(https?:|data:image\/|blob:)/i.test(url))throw Error('This image source is unavailable.');
  await api.ensureApp();const options={url,conflictAction:'uniquify',saveAs:false};
  let name='';if(/^https?:/i.test(url))try{name=decodeURIComponent(new URL(url).pathname.split('/').pop());}catch{}
  if(/\.(png|jpe?g|gif|webp|avif|bmp|tiff?|ico|svg|hei[cf])$/i.test(name))options.filename=safeFilename(name);
  // No canvas conversion or re-encoding: import the received representation.
  return api.download(options);
}
