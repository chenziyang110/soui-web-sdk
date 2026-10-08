'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=__dirname,out='C:/SOUI-SDK-development/preview9-tray/verification/production-validation/preview11-real-renewal-preparation-2026-09-26';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const files=['PROTOCOL.md','consumer/CMakeLists.txt','consumer/main.cpp','consumer/web/main.ts','consumer/web/index.html',
 'Prepare.cjs','Build-Web.mjs','Build.ps1','Run.ps1','Verify-Run.cjs','SelfTest-Verify-Run.cjs',
 'prepared.json','preparation-verify.result.json','preparation-verify.stdout.log','preparation-verify.stderr.log','Archive-Preparation.cjs'];
function walk(rel){return fs.readdirSync(path.join(root,rel),{withFileTypes:true}).flatMap(e=>{
 if(e.isSymbolicLink())throw Error('Linked archive input');const n=rel+'/'+e.name;return e.isDirectory()?walk(n):[n];
})}
files.push(...walk('review-history'));
if(fs.existsSync(path.join(out,'raw')))throw Error('Refuse overwrite archive');
const entries=files.map(name=>({source:name,target:name}));
entries.push({source:'self-test-only/2026-09-26T06-29-51-056Z-49140/self-test-result.json',target:'self-test-result.json'});
const manifest=[];
for(const e of entries.sort((a,b)=>a.target<b.target?-1:1)){
 const b=fs.readFileSync(path.join(root,e.source)),dest=path.join(out,'raw',e.target);
 fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,b,{flag:'wx'});
 if(hash(fs.readFileSync(dest))!==hash(b))throw Error('Archive changed');
 manifest.push({path:'raw/'+e.target,originalPath:e.source,bytes:b.length,sha256:hash(b)});
}
fs.writeFileSync(path.join(out,'raw-manifest.json'),JSON.stringify({schema:'soui.real-renewal-preparation-archive/v1',createdAt:new Date().toISOString(),version:'0.3.0-preview.11',files:manifest,compiled:false,guiExecuted:false,realRenewalVerified:false,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({archiveFiles:manifest.length,bytes:manifest.reduce((n,f)=>n+f.bytes,0),compiled:false,guiExecuted:false}));
