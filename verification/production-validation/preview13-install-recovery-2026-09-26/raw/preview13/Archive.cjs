'use strict';
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview9-tray',prior='C:/SOUI-SDK-production-validation/preview12-install-recovery-20260926';
const destination=path.join(repo,'verification/production-validation/preview13-install-recovery-2026-09-26');
if(fs.existsSync(destination))throw Error('Archive exists');fs.mkdirSync(destination,{recursive:true});
const hash=b=>crypto.createHash('sha256').update(b).digest('hex'),files=[];
const read=p=>JSON.parse(fs.readFileSync(p,'utf8').replace(/^\uFEFF/,''));
function copy(source,relative){const b=fs.readFileSync(source),target=path.join(destination,'raw',relative);fs.mkdirSync(path.dirname(target),{recursive:true});fs.writeFileSync(target,b,{flag:'wx'});files.push({path:'raw/'+relative,source,bytes:b.length,sha256:hash(b)});}
function walk(source,prefix){for(const e of fs.readdirSync(source,{withFileTypes:true})){if(e.isSymbolicLink())throw Error('Unexpected raw link');const p=path.join(source,e.name),r=prefix+'/'+e.name;if(e.isDirectory())walk(p,r);else copy(p,r);}}
function top(source,prefix){for(const e of fs.readdirSync(source,{withFileTypes:true})){if(e.isFile()&&/\.(?:json|cjs|log|snapshot)$/.test(e.name))copy(path.join(source,e.name),prefix+'/'+e.name);}walk(path.join(source,'command-logs'),prefix+'/command-logs');}
for(const name of ['contracts','package-contracts','pack','real-packages','sdk-web-audit','source-matches-package']){
 const report=read(path.join(root,'command-logs',name,'result.json'));assert.equal(report.exitCode,0);assert.equal(report.timedOut,false);assert.equal(report.error,null);
 for(const key of ['stdout','stderr']){const b=fs.readFileSync(path.join(root,'command-logs',name,key+'.log'));assert.equal(hash(b),report[key].sha256);assert.equal(b.length,report[key].bytes);}
}
assert.equal(read(path.join(root,'result.json')).realProcessTermination,true);
assert.equal(read(path.join(root,'command-logs/sdk-web-audit/stdout.log')).metadata.vulnerabilities.total,0);
top(root,'preview13');top(prior,'preview12');top(path.join(prior,'attempt2'),'preview12/attempt2');
for(const [source,label]of [[root,'preview13'],[prior,'preview12']]){
 copy(path.join(source,'sdk-package/sdk-manifest.json'),label+'/package-manifest.json');
 for(const name of ['soui-sdk.cjs','install-transaction.cjs'])copy(path.join(source,'sdk-package/payload/tools/sdk',name),label+'/installer/'+name);
}
for(const name of ['sdk-lock.json','.soui-sdk-transaction/journal.json'])copy(path.join(prior,'attempt2/real-replace',name),'preview12/failed-project/'+name);
const stdout=fs.readFileSync(path.join(root,'command-logs/contracts/stdout.log'),'utf8');
const unitRoot=stdout.match(/Raw interruption samples retained: (.+)/)?.[1].trim();
const allowed=path.resolve(repo,'out/sdk-recovery')+path.sep;
assert(unitRoot&&path.resolve(unitRoot).startsWith(allowed));
for(const e of fs.readdirSync(unitRoot,{withFileTypes:true}))if(e.isFile()&&e.name.endsWith('.json'))copy(path.join(unitRoot,e.name),'unit-process-records/'+e.name);
copy(path.join(repo,'tests/sdk/installation-interruption.test.cjs'),'unit-source/installation-interruption.test.cjs');
copy(path.join(repo,'tests/sdk/installation-interruption-worker.cjs'),'unit-source/installation-interruption-worker.cjs');
for(const name of ['package.json','package-lock.json'])copy(path.join(repo,'sdk/web',name),'audit-input/'+name);
const manifest={schema:'soui.raw-evidence/v1',createdAt:new Date().toISOString(),files:files.sort((a,b)=>a.path.localeCompare(b.path)),releaseReady:false};
fs.writeFileSync(path.join(destination,'raw-manifest.json'),JSON.stringify(manifest,null,2)+'\n',{flag:'wx'});
console.log(JSON.stringify({destination,rawFiles:files.length,rawBytes:files.reduce((n,f)=>n+f.bytes,0),sourceSha256:read(path.join(root,'result.json')).sourceSha256,releaseReady:false}));
