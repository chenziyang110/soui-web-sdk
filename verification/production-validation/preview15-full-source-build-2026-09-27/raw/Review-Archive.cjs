'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const root=__dirname,repo='C:/SOUI-SDK-development/preview14-scheduler',source='C:/SOUI-SDK-source-validation/preview15-no-git';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
function checkCommand(base,expected=0){const r=id.readJson(path.join(base,'result.json'));assert.equal(r.exitCode,expected,base);assert.equal(r.timedOut,false);assert.equal(r.error,null);for(const stream of ['stdout','stderr']){const b=fs.readFileSync(path.join(base,stream+'.log'));assert.equal(b.length,r[stream].bytes);assert.equal(id.hash(b),r[stream].sha256);}return r;}
const packaged=id.readJson(path.join(root,'package-result.json')),relocated=id.readJson(path.join(root,'relocate-result.json'));
assert.equal(id.hash(fs.readFileSync(packaged.archive)),packaged.archiveSha256);assert.equal(packaged.archiveSha256,'4c0255ca2a215e819bbda339d7432fc37398c3cef98b7d234f0411fcd4a4dd92');
assert.equal(relocated.status,'extracted-source-and-sdk-matched');assert.equal(relocated.gitPresent,false);assert(!fs.existsSync(path.join(source,'.git')));
assert.equal(id.hash(fs.readFileSync(path.join(source,'source-manifest.json'))),packaged.sourceManifestSha256);
assert.equal(id.source(source).sha256,relocated.applicationSource.sha256);
for(const name of fs.readdirSync(path.join(root,'command-logs')))checkCommand(path.join(root,'command-logs',name));
const matrix=id.readJson(path.join(root,'matrix/matrix-result.json')),units=id.readJson(path.join(root,'matrix/unit-results.json'));
assert.equal(matrix.status,'four-configurations-compiled');assert.equal(matrix.matrix.length,4);assert.equal(matrix.sourceSha256,relocated.applicationSource.sha256);assert.equal(units.sourceSha256,matrix.sourceSha256);
for(const run of matrix.matrix){
 const base=path.join(root,'matrix/builds',`build-${run.architecture}-${run.configuration.toLowerCase()}`),build=id.readJson(path.join(base,'build.json'));
 assert.equal(build.buildId,run.buildId);assert.equal(build.source.sha256,matrix.sourceSha256);assert.equal(build.version,'0.3.0-preview.15');
 for(const name of ['SouiStarter','SouiWorkbench']){id.verifyPayload(path.join(base,name),name,build.payloads[name]);let count=0;for(const f of build.payloads[name].files.filter(f=>/\.(exe|dll)$/i.test(f.path))){const b=fs.readFileSync(path.join(base,name,f.path));assert.equal(b.readUInt16LE(b.readUInt32LE(0x3c)+4),run.architecture==='x86'?0x14c:0x8664);count++;}assert.equal(count,60);}
 for(const file of id.walk(path.join(base,'commands'))){const b=fs.readFileSync(path.join(base,'commands',file));assert(!/[\\/]vcpkg[\\/]/i.test(b[0]===255&&b[1]===254?b.toString('utf16le'):b.toString('utf8')));}
}
for(const name of fs.readdirSync(path.join(root,'matrix/command-logs'))){const base=path.join(root,'matrix/command-logs',name);checkCommand(base);if(name.startsWith('ctest-')||name.startsWith('unit-contract-'))assert.match(fs.readFileSync(path.join(base,'stdout.log'),'utf8'),/100% tests passed, 0 tests failed out of 12/);}
const blocked=path.join(root,'desktop/command-logs/x86-apps-attempt1');checkCommand(blocked,2);
const gate=JSON.parse(fs.readFileSync(path.join(blocked,'stdout.log'),'utf8'));assert.equal(gate.status,'busy-no-launch');assert.match(gate.first.stdout,/qemu-system-x86_64/);assert(!fs.existsSync(path.join(root,'desktop/runs')));
const report={version:'0.3.0-preview.15',archiveSha256:packaged.archiveSha256,archiveBytes:packaged.archiveBytes,sourceManifestSha256:packaged.sourceManifestSha256,sourceFiles:34612,sdkFiles:419,sdkManifestSha256:relocated.sdkManifestSha256,sourceSha256:matrix.sourceSha256,matrix:matrix.matrix,ctestCases:48,fullUnitEntryPassed:true,fullSourceVerifiedBeforeAndAfter:true,sameVersionSdkMatchedBeforeAndAfter:true,allCopiedPayloadsMatched:true,guiExecuted:false,guiBlockedByExternalQemu:true,sourceDeliveryVerified:false,releaseReady:false};
fs.writeFileSync(path.join(root,'review.json'),id.encoded(report),{flag:'wx'});
const target=path.join(repo,'verification/production-validation/preview15-full-source-build-2026-09-27');assert(!fs.existsSync(target));const files=[];
for(const name of id.walk(root,'',(_,leaf)=>['SouiStarter','SouiWorkbench'].includes(leaf))){const original=path.join(root,name),bytes=fs.readFileSync(original),dest=path.join(target,'raw',name);fs.mkdirSync(path.dirname(dest),{recursive:true});fs.writeFileSync(dest,bytes,{flag:'wx'});files.push({path:'raw/'+name,original,bytes:bytes.length,sha256:id.hash(bytes)});}
fs.writeFileSync(path.join(target,'raw-manifest.json'),id.encoded({schema:'soui.raw-evidence/v1',files}),{flag:'wx'});
console.log(JSON.stringify({rawFiles:files.length,ctestCases:48,configurations:matrix.matrix.length,guiExecuted:false,releaseReady:false}));
