'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview9-tray',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
function archive(name,sources){const target=path.join(repo,'verification/production-validation',name);assert(!fs.existsSync(target));const files=[];
 const copy=(source,relative)=>{const b=fs.readFileSync(source),to=path.join(target,'raw',relative);fs.mkdirSync(path.dirname(to),{recursive:true});fs.writeFileSync(to,b,{flag:'wx'});files.push({path:'raw/'+relative,bytes:b.length,sha256:id.hash(b),original:source});};
 for(const [label,root,exclude]of sources)for(const file of id.walk(root,'',exclude))copy(path.join(root,file),label+'/'+file);
 files.sort((a,b)=>a.path.localeCompare(b.path));fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});return{name,files:files.length,bytes:files.reduce((n,f)=>n+f.bytes,0)};}
const results=[];
results.push(archive('preview13-legal-identity-2026-09-26',[
 ['source-build-failure','C:/SOUI-SDK-production-validation/preview13-full-source-build-20260926',(_,leaf)=>leaf==='source'],
 ['source-package-retry','C:/SOUI-SDK-production-validation/preview13-source-package-attempt2-20260926',()=>false],
 ['source-package-first','C:/SOUI-SDK-production-validation/preview13-independent-build-isolated-20260926/command-logs/source-package',()=>false],
 ['module-diagnostic','C:/SOUI-SDK-production-validation/preview13-independent-build-isolated-20260926/command-logs/package-environment',()=>false]
]));
results.push(archive('preview11-renewal-page-stages-2026-09-26',[
 ['page-stages','C:/SOUI-SDK-production-validation/preview11-renewal-page-stages-preparation-20260926',(relative,leaf)=>leaf==='input'||leaf==='runtime'],
 ['prelude-stages','C:/SOUI-SDK-production-validation/preview11-renewal-prelude-stages-preparation-20260926',(relative,leaf)=>leaf==='input'||leaf==='runtime']
]));
console.log(id.encoded(results));
