'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview9-tray',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
function archive(name,sources){const target=path.join(repo,'verification/production-validation',name);assert(!fs.existsSync(target));const files=[];
 for(const [label,root,exclude]of sources)for(const file of id.walk(root,'',exclude)){const original=path.join(root,file),b=fs.readFileSync(original),relative='raw/'+label+'/'+file.replace(/(^|\/)\.gitattributes$/,'$1gitattributes.snapshot'),to=path.join(target,relative);fs.mkdirSync(path.dirname(to),{recursive:true});fs.writeFileSync(to,b,{flag:'wx'});files.push({path:relative,bytes:b.length,sha256:id.hash(b),original});}
 files.sort((a,b)=>a.path.localeCompare(b.path));fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});return{name,files:files.length,bytes:files.reduce((n,f)=>n+f.bytes,0)};
}
assert(fs.existsSync(path.join(__dirname,'independent-review.json')));
console.log(id.encoded(archive('preview13-full-source-build-2026-09-26',[
 ['source-build',__dirname,(_,leaf)=>['source','SouiStarter','SouiWorkbench'].includes(leaf)],
 ['package','C:/SOUI-SDK-production-validation/preview13-legal-identity-20260926',(_,leaf)=>leaf==='PR.md']
])));
