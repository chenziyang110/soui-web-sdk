'use strict';
const fs=require('node:fs'), path=require('node:path'), crypto=require('node:crypto');
const suite=__dirname, repo='C:/SOUI-SDK-development/preview9-tray';
const target=path.join(repo,'verification/production-validation/preview11-session-renewal-preflight-2026-09-26');
const raw=path.join(target,'raw');
if(fs.existsSync(raw)) throw new Error('Do not overwrite archived evidence');
const names=fs.readdirSync(suite,{withFileTypes:true}).filter(e=>e.isFile()&&/\.(?:json|log|ps1|cjs)$/.test(e.name)).map(e=>e.name);
for(const arch of ['x86','x64']){
  const dir=`${arch}-attempt1`;
  for(const entry of fs.readdirSync(path.join(suite,dir),{withFileTypes:true})){
    if(entry.isFile()&&/\.(?:json|log|xml)$/.test(entry.name)) names.push(`${dir}/${entry.name}`);
  }
  names.push(`${dir}/consumer-build/CMakeCache.txt`,`${dir}/consumer-build/soui_session_consumer.vcxproj`);
  for(const entry of fs.readdirSync(path.join(suite,`tray-${arch}-attempt1`),{withFileTypes:true})){
    if(entry.isFile()&&/\.(?:json|log)$/.test(entry.name)) names.push(`tray-${arch}-attempt1/${entry.name}`);
  }
}
for(const entry of fs.readdirSync(path.join(suite,'desktop-attempt1'),{withFileTypes:true})){
  if(entry.isFile())names.push(`desktop-attempt1/${entry.name}`);
}
names.push('sdk-package-attempt1/sdk-manifest.json');
const files=[];
for(const name of names.sort()){
  const data=fs.readFileSync(path.join(suite,name)), dest=path.join(raw,name);
  fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,data,{flag:'wx'});
  files.push({path:`raw/${name}`,bytes:data.length,sha256:crypto.createHash('sha256').update(data).digest('hex')});
}
fs.writeFileSync(path.join(target,'raw-manifest.json'),JSON.stringify({schema:'soui.raw-evidence/v1',sourceDirectory:suite,files},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({archivedFiles:files.length,bytes:files.reduce((sum,f)=>sum+f.bytes,0)}));
