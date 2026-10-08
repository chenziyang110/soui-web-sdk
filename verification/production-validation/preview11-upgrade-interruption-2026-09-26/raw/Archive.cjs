'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const root=__dirname,repo='C:/SOUI-SDK-development/preview9-tray',dest=path.join(repo,'verification/production-validation/preview11-upgrade-interruption-2026-09-26');
const hash=b=>crypto.createHash('sha256').update(b).digest('hex');
const files=[];
function copy(source,relative){
 const data=fs.readFileSync(source),target=path.join(dest,'raw',relative);
 fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,data,{flag:'wx'});
 if(!data.equals(fs.readFileSync(target)))throw Error('Archive copy differs');
 files.push({path:'raw/'+relative,source,bytes:data.length,sha256:hash(data)});
}
function tree(base,prefix,exclude=()=>false){
 for(const e of fs.readdirSync(base,{withFileTypes:true})){if(e.isSymbolicLink())throw Error('Linked evidence');if(exclude(e.name))continue;const p=path.join(base,e.name),r=prefix+'/'+e.name;if(e.isDirectory())tree(p,r,exclude);else copy(p,r);}
}
if(fs.existsSync(dest))throw Error('Archive exists');fs.mkdirSync(dest,{recursive:true});
for(const f of fs.readdirSync(root)){if(/\.cjs$/.test(f)||['inputs.json','business-input.json','verification.json'].includes(f))copy(path.join(root,f),f);}
tree(path.join(root,'logs'),'preparation-logs');tree(path.join(root,'command-logs'),'command-logs');
tree(path.join(root,'stages'),'stages',n=>n==='runtime');
tree(path.join(root,'inputs/old-generator'),'old-generator');
const business=JSON.parse(fs.readFileSync(path.join(root,'business-input.json'),'utf8'));
for(const f of business.files)copy(path.join(root,'project',f.path),'business-source/'+f.path);
tree(path.join(root,'interruption-attempt1'),'interruption-attempt1',n=>n==='project');
const failed='C:/SOUI-SDK-production-validation/preview11-upgrade-20260926';copy(path.join(failed,'Prepare.cjs'),'failed-preparation/Prepare.cjs');tree(path.join(failed,'logs'),'failed-preparation/logs');
files.sort((a,b)=>a.path.localeCompare(b.path));
const manifest={schema:'soui.raw-evidence/v1',createdAt:new Date().toISOString(),files,guiExecuted:false,releaseReady:false};
fs.writeFileSync(path.join(dest,'raw-manifest.json'),JSON.stringify(manifest,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({dest,files:files.length,bytes:files.reduce((n,f)=>n+f.bytes,0),manifestSha256:hash(fs.readFileSync(path.join(dest,'raw-manifest.json')))}));
