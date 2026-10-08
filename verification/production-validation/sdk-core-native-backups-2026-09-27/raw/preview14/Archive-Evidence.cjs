'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const target=path.join(repo,'verification/production-validation/sdk-core-native-backups-2026-09-27');
assert(!fs.existsSync(target));
for(const [name,code] of [['package-red',1],['package-red-attempt2',1],['package-green',0],['package-smoke',0],['delivery-tests',0],['interruption-tests',0],['real-package-roundtrip',0],['sbom-sdk',0],['sbom-template',0],['sbom-demo',0],['verify-source',0],['audit-sdk',0]]){
 const result=id.readJson(path.join(root,'command-logs',name,'result.json'));assert.equal(result.exitCode,code);assert.equal(result.timedOut,false);assert.equal(result.error,null);
 for(const stream of ['stdout','stderr']){const bytes=fs.readFileSync(path.join(root,'command-logs',name,stream+'.log'));assert.equal(bytes.length,result[stream].bytes);assert.equal(id.hash(bytes),result[stream].sha256);}
}
assert.match(fs.readFileSync(path.join(root,'command-logs/package-red-attempt2/stdout.log'),'utf8'),/must not be delivered/);
const review=id.readJson(path.join(root,'package-review.json'));assert.equal(review.version,'0.3.0-preview.14');assert.equal(review.steps.length,4);
for(const [name,dest] of [['sdk','sbom-web-sdk.cdx.json'],['template','sbom-template.cdx.json'],['demo','sbom-demo.cdx.json']]){
 const file=path.join(root,'command-logs','sbom-'+name,'stdout.log'),bom=id.readJson(file);assert.equal(bom.bomFormat,'CycloneDX');
 if(name==='sdk')assert.equal(bom.metadata.component.version,'0.3.0-preview.14');
 fs.copyFileSync(file,path.join(repo,'verification',dest));
}
const files=[];
function copy(original,relative){const bytes=fs.readFileSync(original),dest=path.join(target,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes,{flag:'wx'});files.push({path:relative,original,bytes:bytes.length,sha256:id.hash(bytes)});}
for(const name of id.walk(root,'',(_,leaf)=>['sdk-package','independent-project'].includes(leaf)))copy(path.join(root,name),'raw/'+name);
copy(path.join(root,'sdk-package/sdk-manifest.json'),'raw/manifests/sdk-preview14.json');
copy('C:/SOUI-SDK-production-validation/preview13-install-recovery-20260926/sdk-package/sdk-manifest.json','raw/manifests/sdk-preview13.json');
for(const name of ['tests/sdk/sdk-native-payload.test.cjs','tools/sdk/soui-sdk.cjs','tools/internal/native-payload.cjs','tools/internal/Update-DependencyManifest.cjs','third_party/manifests/dependencies.json','third_party/manifests/license-review.json','third_party/manifests/kernel-build-preview14.json'])copy(path.join(repo,name),'raw/current/'+name);
files.sort((a,b)=>a.path.localeCompare(b.path));
for(const f of files){assert.equal(id.hash(fs.readFileSync(f.original)),f.sha256);assert.equal(id.hash(fs.readFileSync(path.join(target,f.path))),f.sha256);}
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(JSON.stringify({files:files.length,version:review.version,oldBytes:review.oldPayloadBytes,newBytes:review.newPayloadBytes,netReductionBytes:review.netReductionBytes}));
