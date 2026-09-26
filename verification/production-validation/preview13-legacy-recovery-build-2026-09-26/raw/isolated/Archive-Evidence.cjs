'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview9-tray',root=__dirname,legacy='C:/SOUI-SDK-production-validation/preview13-legacy-recovery-build-20260926';
const destination=path.join(repo,'verification/production-validation/preview13-legacy-recovery-build-2026-09-26');
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
assert(!fs.existsSync(destination));
const entries=[];
function copy(source,name){const bytes=fs.readFileSync(source),target=id.safePath(destination,'raw/'+name);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,bytes,{flag:'wx'});entries.push({path:'raw/'+name,bytes:bytes.length,sha256:id.hash(bytes),original:source});}
for(const [name,directory] of [['legacy',legacy],['isolated',root]]){
 for(const f of id.walk(directory,'',(relative,leaf)=>['project','legacy-evidence','restore-stage'].includes(leaf)||/^builds\/[^/]+\/runtime(?:\/|$)/.test(relative))){
  assert(!['.gitattributes','.gitignore','AGENTS.md'].includes(path.basename(f)));
  copy(path.join(directory,f),name+'/'+f);
 }
}
for(const row of id.readJson(path.join(legacy,'matrix-result.json')).matrix){
 const name='build-'+row.architecture+'-'+row.configuration.toLowerCase();
 for(const kind of ['CL','link']){const f=kind+'.command.1.tlog';copy(path.join(legacy,'project/out',row.architecture,'UpgradeLedger.dir',row.configuration,'UpgradeLedger.tlog',f),'legacy/builds/'+name+'/logs/'+f);}
}
const business=id.readJson(path.join(legacy,'business-input.json'));
for(const f of business.files)copy(path.join(legacy,'project',f.path),'business/'+f.path+'.snapshot');
entries.sort((a,b)=>a.path.localeCompare(b.path));
fs.writeFileSync(path.join(destination,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',createdAt:new Date().toISOString(),files:entries}),{flag:'wx'});
console.log(id.encoded({archivedFiles:entries.length,bytes:entries.reduce((n,f)=>n+f.bytes,0),destination}));
