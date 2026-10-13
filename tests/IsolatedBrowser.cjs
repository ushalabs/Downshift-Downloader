const fs=require('node:fs'),path=require('node:path');
function isolatedBrowser(repo,root,hostInput){
 const host=path.join(path.dirname(hostInput),'DownshiftTestNativeHost.exe');
 if(!fs.existsSync(host))throw Error('Build the isolated native test host first.');
 const extension=path.join(root,'extension');fs.cpSync(path.join(repo,'extension'),extension,{recursive:true});
 const worker=path.join(extension,'background.js');fs.writeFileSync(worker,fs.readFileSync(worker,'utf8').replaceAll('org.ushalabs.downshift','org.ushalabs.downshift.test'));
 return {host,extension};
}
module.exports={isolatedBrowser};
