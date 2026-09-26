'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const base='C:/SOUI-SDK-production-validation',repo='C:/SOUI-SDK-development/preview9-tray';
const sha=b=>crypto.createHash('sha256').update(b).digest('hex');
function archive(name,sources){
 const dest=path.join(repo,'verification/production-validation',name);if(fs.existsSync(dest))throw Error('Archive exists');fs.mkdirSync(dest,{recursive:true});const files=[];
 function copy(source,rel){const b=fs.readFileSync(source),p=path.join(dest,'raw',rel);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b,{flag:'wx'});if(!b.equals(fs.readFileSync(p)))throw Error('Copy mismatch');files.push({path:'raw/'+rel,source,bytes:b.length,sha256:sha(b)});}
 function walk(source,rel){for(const e of fs.readdirSync(source,{withFileTypes:true})){if(e.isSymbolicLink())throw Error('Linked evidence');if(e.name==='runtime')continue;const s=path.join(source,e.name),r=path.posix.join(rel,e.name);if(e.isDirectory())walk(s,r);else copy(s,r);}}
 for(const [prefix,source]of sources)walk(path.join(base,source),prefix);
 copy(__filename,'Archive-Desktop-Failures-20260926.cjs');copy(path.join(base,'Capture-OwnedProbe.ps1'),'Capture-OwnedProbe.ps1');
 files.sort((a,b)=>a.path.localeCompare(b.path));fs.writeFileSync(path.join(dest,'raw-manifest.json'),JSON.stringify({schema:'soui.raw-evidence/v1',createdAt:new Date().toISOString(),files,releaseReady:false},null,2)+'\n',{flag:'wx'});
 console.log(JSON.stringify({name,files:files.length,bytes:files.reduce((n,f)=>n+f.bytes,0),manifestSha256:sha(fs.readFileSync(path.join(dest,'raw-manifest.json')))}));
}
archive('win7-sp1-x86-qemu-preview11-apps-attempt3-2026-09-26',[['attempt3','preview11-win7-apps-attempt3']]);
archive('preview11-real-renewal-v2-smoke-2026-09-26',[
 ['x86-smoke-attempt1','preview11-real-renewal-runs-v2-20260926/x86-smoke-attempt1'],
 ['x64-smoke-attempt1','preview11-real-renewal-runs-v2-20260926/x64-smoke-attempt1'],
 ['diagnostic-attempt1','preview11-renewal-page-diagnostic-20260926'],
 ['diagnostic-attempt2','preview11-renewal-page-diagnostic-attempt2-20260926'],
 ['diagnostic-attempt3','preview11-renewal-page-diagnostic-attempt3-20260926']]);
