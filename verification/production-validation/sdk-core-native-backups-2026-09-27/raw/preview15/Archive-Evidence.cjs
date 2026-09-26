'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler';
const prior='C:/SOUI-SDK-production-validation/sdk-core-native-backups-20260927';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const target=path.join(repo,'verification/production-validation/sdk-core-native-backups-2026-09-27');assert(!fs.existsSync(target));
function check(base,name,code){
 const result=id.readJson(path.join(base,'command-logs',name,'result.json'));assert.equal(result.exitCode,code);assert.equal(result.timedOut,false);assert.equal(result.error,null);
 for(const stream of ['stdout','stderr']){const bytes=fs.readFileSync(path.join(base,'command-logs',name,stream+'.log'));assert.equal(bytes.length,result[stream].bytes);assert.equal(id.hash(bytes),result[stream].sha256);}
}
for(const [name,code] of [['package-red',1],['package-red-attempt2',1],['package-green',0],['package-smoke',0],['delivery-tests',0],['interruption-tests',0],['real-package-roundtrip',0],['sbom-sdk',0],['sbom-template',0],['sbom-demo',0],['verify-source',1],['audit-sdk',0],['selector-correction-tests',1],['selector-correction-tests-attempt2',0]])check(prior,name,code);
for(const name of ['package-and-source-tests','real-package-roundtrip','sbom-sdk'])check(root,name,0);
assert.match(fs.readFileSync(path.join(prior,'command-logs/verify-source/stderr.log'),'utf8'),/source file set changed/);
const review=id.readJson(path.join(root,'package-review.json'));assert.equal(review.version,'0.3.0-preview.15');assert.equal(review.steps.length,4);
for(const [base,name,dest] of [[root,'sdk','sbom-web-sdk.cdx.json'],[prior,'template','sbom-template.cdx.json'],[prior,'demo','sbom-demo.cdx.json']]){
 const file=path.join(base,'command-logs','sbom-'+name,'stdout.log'),bom=id.readJson(file);assert.equal(bom.bomFormat,'CycloneDX');
 if(name==='sdk')assert.equal(bom.metadata.component.version,'0.3.0-preview.15');fs.copyFileSync(file,path.join(repo,'verification',dest));
}
const files=[];
function copy(original,relative){const bytes=fs.readFileSync(original),dest=path.join(target,relative);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes,{flag:'wx'});files.push({path:relative,original,bytes:bytes.length,sha256:id.hash(bytes)});}
for(const [base,prefix] of [[prior,'preview14'],[root,'preview15']]){
 for(const name of id.walk(base,'',(_,leaf)=>['sdk-package','independent-project'].includes(leaf)))copy(path.join(base,name),'raw/'+prefix+'/'+name);
 copy(path.join(base,'sdk-package/sdk-manifest.json'),'raw/manifests/sdk-'+prefix+'.json');
}
copy('C:/SOUI-SDK-production-validation/preview13-install-recovery-20260926/sdk-package/sdk-manifest.json','raw/manifests/sdk-preview13.json');
const matrix='C:/SOUI-SDK-production-validation/preview14-app-matrix-20260927';
check(matrix,'build-x86-release',0);
for(const name of id.walk(matrix,'',(_,leaf)=>['SouiStarter','SouiWorkbench'].includes(leaf)))copy(path.join(matrix,name),'raw/preview14-build/'+name);
for(const name of ['tests/sdk/sdk-native-payload.test.cjs','tools/sdk/soui-sdk.cjs','tools/internal/native-payload.cjs','tools/internal/Update-DependencyManifest.cjs','third_party/manifests/dependencies.json','third_party/manifests/license-review.json','third_party/manifests/kernel-build-preview14.json'])copy(path.join(repo,name),'raw/current-preview15/'+name);
files.sort((a,b)=>a.path.localeCompare(b.path));
for(const f of files){assert.equal(id.hash(fs.readFileSync(f.original)),f.sha256);assert.equal(id.hash(fs.readFileSync(path.join(target,f.path))),f.sha256);}
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(JSON.stringify({files:files.length,version:review.version,oldBytes:review.oldPayloadBytes,newBytes:review.newPayloadBytes,netReductionBytes:review.netReductionBytes,newManifestSha256:review.newManifestSha256}));
