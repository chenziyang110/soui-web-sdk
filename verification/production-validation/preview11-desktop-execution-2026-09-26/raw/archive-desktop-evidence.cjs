'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const suite=__dirname,repo='C:/SOUI-SDK-development/preview9-tray';
const output=path.join(repo,'verification/production-validation/preview11-desktop-execution-2026-09-26');
const hash=data=>crypto.createHash('sha256').update(data).digest('hex');
const walk=(base,rel='')=>fs.readdirSync(path.join(base,rel),{withFileTypes:true}).flatMap(e=>{
 if(e.isSymbolicLink())throw Error('Linked evidence');const name=rel?rel+'/'+e.name:e.name;
 return e.isDirectory()?walk(base,name):[name];
});
if(fs.existsSync(path.join(output,'raw')))throw Error('Refuse overwrite archive');
const directories=['desktop-attempt2','desktop-applications-attempt1','desktop-smoke-attempt1','desktop-direct-smoke-attempt1'];
const names=directories.flatMap(dir=>walk(path.join(suite,dir)).map(n=>dir+'/'+n)).concat([
 'desktop-attempt2-preflight.json','generated-desktop-preflight.json','desktop-independent-review.json',
 'desktop-gate-after-smokes-20260926.json','run-desktop.ps1','run-desktop-applications.ps1',
 'run-desktop-smoke.ps1','run-direct-smoke.ps1','archive-desktop-evidence.cjs','source-inputs.json',
 'generated-x86-build-Release.json','generated-x64-build-Release.json',
 'x86-attempt1/full-build-build-Release.json','x64-attempt1/full-build-build-Release.json'
]);
// Existing preflight archive retains the complete native build logs and SDK receipts.
const files=[];
for(const name of [...new Set(names)].sort()){
 const from=path.join(suite,name);if(!fs.existsSync(from))throw Error('Missing evidence '+name);
 const bytes=fs.readFileSync(from),to=path.join(output,'raw',name);
 fs.mkdirSync(path.dirname(to),{recursive:true});fs.writeFileSync(to,bytes,{flag:'wx'});
 if(hash(fs.readFileSync(to))!==hash(bytes))throw Error('Copy differs');
 files.push({path:'raw/'+name,bytes:bytes.length,sha256:hash(bytes)});
}
fs.writeFileSync(path.join(output,'raw-manifest.json'),JSON.stringify({schema:'soui.evidence-archive/v1',version:'0.3.0-preview.11',createdAt:new Date().toISOString(),sourceInputSha256:'a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550',files,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({files:files.length,bytes:files.reduce((n,f)=>n+f.bytes,0),output,releaseReady:false}));
