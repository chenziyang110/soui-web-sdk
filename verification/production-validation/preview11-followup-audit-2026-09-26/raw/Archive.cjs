'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto');
const dest='C:/SOUI-SDK-development/preview9-tray/verification/production-validation/preview11-followup-audit-2026-09-26',project='C:/SOUI-SDK-production-validation/preview11-upgrade-attempt2-20260926/project';
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
if(fs.existsSync(dest))throw Error('Audit archive exists');fs.mkdirSync(dest,{recursive:true});const files=[];
function copy(src,relative){const b=fs.readFileSync(src),p=path.join(dest,'raw',relative);fs.mkdirSync(path.dirname(p),{recursive:true});fs.writeFileSync(p,b,{flag:'wx'});files.push({path:'raw/'+relative,source:src,bytes:b.length,sha256:hash(b)});}
function tree(p,rel){for(const e of fs.readdirSync(p,{withFileTypes:true})){if(e.isSymbolicLink())throw Error('Linked evidence');if(e.isDirectory())tree(path.join(p,e.name),rel+'/'+e.name);else copy(path.join(p,e.name),rel+'/'+e.name);}}
const audits=[];
for(const name of ['web-audit','sdk-web-audit']){
 const dir=path.join(__dirname,'command-logs',name),result=read(path.join(dir,'result.json')),report=read(path.join(dir,'stdout.log'));
 if(result.exitCode!==0||result.timedOut||result.error||report.metadata?.vulnerabilities?.total!==0)throw Error('Audit did not pass');
 for(const stream of ['stdout','stderr']){const b=fs.readFileSync(path.join(dir,stream+'.log'));if(b.length!==result[stream].bytes||hash(b)!==result[stream].sha256)throw Error('Audit log changed');}
 audits.push({name,exitCode:result.exitCode,vulnerabilities:report.metadata.vulnerabilities});
}
for(const f of read(path.join(path.dirname(project),'business-input.json')).files){const b=fs.readFileSync(path.join(project,f.path));if(b.length!==f.bytes||hash(b)!==f.sha256)throw Error('Business changed');}
tree(path.join(__dirname,'command-logs'),'command-logs');
for(const r of ['web','sdk/web'])for(const f of ['package.json','package-lock.json'])copy(path.join(project,r,f),r+'/'+f);
copy(path.join(__dirname,'Capture-Command.cjs'),'Capture-Command.cjs');copy(__filename,'Archive.cjs');
fs.writeFileSync(path.join(dest,'raw-manifest.json'),JSON.stringify({schema:'soui.raw-evidence/v1',files,audits,releaseReady:false},null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({files:files.length,audits}));
