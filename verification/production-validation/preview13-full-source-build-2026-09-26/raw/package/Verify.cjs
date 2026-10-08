'use strict';
const fs=require('node:fs'),path=require('node:path'),cp=require('node:child_process'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview9-tray',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
for(const name of ['preview13-legal-identity-2026-09-26','preview11-renewal-page-stages-2026-09-26']){
 const prefix='verification/production-validation/'+name,root=path.join(repo,prefix),manifest=id.readJson(path.join(root,'raw-manifest.json'));
 assert.deepEqual(id.walk(root,'raw').sort(),manifest.files.map(f=>f.path).sort());
 const result=cp.spawnSync('git',['cat-file','--batch'],{cwd:repo,input:manifest.files.map(f=>':'+prefix+'/'+f.path+'\n').join(''),maxBuffer:16*1024*1024,windowsHide:true});assert.equal(result.status,0);let cursor=0;
 for(const f of manifest.files){for(const file of [path.join(root,f.path),f.original]){const b=fs.readFileSync(file);assert.equal(b.length,f.bytes,file);assert.equal(id.hash(b),f.sha256,file);}const end=result.stdout.indexOf(10,cursor),match=result.stdout.subarray(cursor,end).toString().match(/^[a-f0-9]{40} blob (\d+)$/);assert(match,f.path);const size=Number(match[1]),b=result.stdout.subarray(end+1,end+1+size);assert.equal(size,f.bytes);assert.equal(id.hash(b),f.sha256,f.path);cursor=end+size+2;}assert.equal(cursor,result.stdout.length);
 console.log(id.encoded({archive:name,files:manifest.files.length,rawDiskOriginalAndIndexVerified:true}));
}
const license='third_party/licenses/element-plus-2.11.1-LICENSE.txt',result=cp.spawnSync('git',['show',':'+license],{cwd:repo,windowsHide:true});assert.equal(result.status,0);assert.equal(id.hash(result.stdout),'7d6560ca47099f18ae7dc4d2fcdb3e0dcc207430a442525177f8af6dc229d1cc');
const run='C:/SOUI-SDK-production-validation/preview11-renewal-page-stages-preparation-20260926/runs/2026-09-26T11-37-54-934Z-65224';const lines=fs.readFileSync(path.join(run,'native.ndjson'),'utf8').trim().split('\n').map(JSON.parse);assert.equal(lines.length,19);const last=lines.at(-1);assert.equal(last.event,'run.finished');assert.equal(last.exitCode,1);assert.equal(last.readyCount,0);assert.equal(last.bootstrapCount,0);assert.equal(last.longInvocations,0);assert.equal(last.resultInvocations,0);
assert(id.readJson(path.join(run,'gates-during.json')).every(r=>r.exitCode===0&&!r.error));assert.equal(id.readJson(path.join(run,'gate-after.json')).exitCode,0);
assert.equal(id.source(repo).sha256,'a1007f2188102dfa4de4e79f73a4e0b2c2b8b99cf3e892a3d1e1e3794fd51e68');
console.log(id.encoded({licenseIndexMatchesSdk:true,nativeFailureRecords:lines.length,sourceSha256:id.source(repo).sha256,releaseReady:false}));
