'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler',id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const input=id.readJson(path.join(root,'matrix-input.json')),result=id.readJson(path.join(root,'matrix-result.json')),units=id.readJson(path.join(root,'unit-results.json'));
assert.equal(result.status,'four-configurations-compiled');assert.equal(result.version,'0.3.0-preview.15');assert.equal(result.matrix.length,4);
assert.equal(id.source(repo).sha256,input.source.sha256);assert.equal(units.sourceSha256,input.source.sha256);
for(const run of result.matrix){
 const name=`build-${run.architecture}-${run.configuration.toLowerCase()}`,dir=path.join(root,'builds',name),build=id.readJson(path.join(dir,'build.json'));
 assert.equal(build.source.sha256,input.source.sha256);assert.equal(build.buildId,run.buildId);
 for(const target of ['SouiStarter','SouiWorkbench'])id.verifyPayload(path.join(dir,target),target,build.payloads[target]);
}
for(const name of fs.readdirSync(path.join(root,'command-logs'))){
 const dir=path.join(root,'command-logs',name),record=id.readJson(path.join(dir,'result.json'));assert.equal(record.exitCode,0);assert.equal(record.timedOut,false);assert.equal(record.error,null);
 for(const stream of ['stdout','stderr']){const bytes=fs.readFileSync(path.join(dir,stream+'.log'));assert.equal(bytes.length,record[stream].bytes);assert.equal(id.hash(bytes),record[stream].sha256);}
 if(name.startsWith('ctest-')||name.startsWith('unit-contract'))assert.match(fs.readFileSync(path.join(dir,'stdout.log'),'utf8'),/100% tests passed, 0 tests failed out of 12/);
}
const review={version:result.version,sourceSha256:input.source.sha256,sourceFiles:input.source.files.length,matrix:result.matrix,
  unitCommands:units.results,ctestCases:48,fullUnitEntryPassed:true,allPayloadsMatched:true,ambientVcpkgPathsAbsent:true,guiExecuted:false,sourceDeliveryVerified:false,releaseReady:false};
fs.writeFileSync(path.join(root,'independent-review.json'),id.encoded(review),{flag:'wx'});
const target=path.join(repo,'verification/production-validation/preview15-app-matrix-2026-09-27');assert(!fs.existsSync(target));const files=[];
for(const name of id.walk(root,'',(_,leaf)=>['SouiStarter','SouiWorkbench'].includes(leaf))){const original=path.join(root,name),bytes=fs.readFileSync(original),dest=path.join(target,'raw',name);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes,{flag:'wx'});files.push({path:'raw/'+name,original,bytes:bytes.length,sha256:id.hash(bytes)});}
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(JSON.stringify({rawFiles:files.length,sourceFiles:review.sourceFiles,sourceSha256:review.sourceSha256,ctestCases:48,guiExecuted:false}));
