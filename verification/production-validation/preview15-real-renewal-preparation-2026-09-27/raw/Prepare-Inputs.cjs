'use strict';
// Called only after both actual SDK-only builds/installations have finished.
const fs=require('node:fs'),path=require('node:path');
module.exports=function(ctx){
 const {ROOT,SDK,INPUTS,SOURCE,VERSION,encode,hash,read,assert,write,walk,snapshot,verify,copyFiles,checkRecord,identityTool,checkSdkSource}=ctx;
 assert(!fs.existsSync(INPUTS)&&!fs.existsSync(path.join(ROOT,'prepared.json'))&&!fs.existsSync(path.join(ROOT,'toolchain')),'Inputs already prepared');
 const staging=read(path.join(ROOT,'staging-input.json')),done=read(path.join(ROOT,'sdk-build-result.json'));
 assert(staging.sourceSha256===SOURCE&&staging.version===VERSION&&done.status==='two-sdk-installations-built','Current staging/builds missing');
 verify(SDK,staging.snapshot,false);checkSdkSource();
 const fullRoot='C:/SOUI-SDK-production-validation/preview15-app-matrix-20260927/builds';
 fs.mkdirSync(INPUTS);const source=identityTool().source(SDK);copyFiles(SDK,path.join(INPUTS,'source'),source.files.map(f=>f.path));
 const metadata=staging.snapshot.files.map(f=>f.path).filter(n=>!source.files.some(f=>f.path===n));
 if(metadata.length)copyFiles(SDK,path.join(INPUTS,'source-sdk-metadata'),metadata);
 fs.mkdirSync(path.join(INPUTS,'records'));const architectures={};
 for(const arch of ['x86','x64']){
  const fullDir=path.join(fullRoot,'build-'+arch+'-release'),full=read(path.join(fullDir,'build.json'));
  const sdkPath=path.join(SDK,'out','sdk-'+arch,'build-Release.json'),sdk=read(sdkPath);
  checkRecord(full,arch,false);checkRecord(sdk,arch,true);
  const recorded=done.architectures.find(r=>r.architecture===arch);assert(recorded&&recorded.sdkBuildId===sdk.buildId,'SDK build ID mismatch');
  const installRoot=path.join(SDK,'out/install',arch),installed=snapshot(installRoot);
  assert(recorded.installedSdk.sha256===installed.sha256,'Installed SDK changed');
  const library=sdk.library.files.find(f=>f.path==='Release/soui_sdk.lib'),installedLibrary=installed.files.find(f=>f.path==='lib/Release/soui_sdk.lib');
  assert(library&&installedLibrary&&library.bytes===installedLibrary.bytes&&library.sha256===installedLibrary.sha256,'Installed library differs from actual build');
  copyFiles(installRoot,path.join(INPUTS,'install',arch),installed.files.map(f=>f.path));
  const runtimeRoot=path.join(fullDir,'SouiStarter');identityTool().verifyPayload(runtimeRoot,'SouiStarter',full.payloads.SouiStarter);
  const names=full.payloads.SouiStarter.files.map(f=>f.path).filter(n=>/^[^/]+\.dll$/i.test(n)||n.startsWith('resources/legal/'));
  assert(names.includes('node.dll')&&names.filter(n=>n.startsWith('resources/legal/')).length===5,'Missing runtime/legal inputs');
  const runtimeBase=copyFiles(runtimeRoot,path.join(INPUTS,'runtime-base',arch),names);
  fs.copyFileSync(sdkPath,path.join(INPUTS,'records','sdk-build-'+arch+'.json'),fs.constants.COPYFILE_EXCL);
  fs.copyFileSync(path.join(fullDir,'build.json'),path.join(INPUTS,'records','full-build-'+arch+'.json'),fs.constants.COPYFILE_EXCL);
  copyFiles(path.join(ROOT,'command-logs','sdk-'+arch),path.join(INPUTS,'records','sdk-command-'+arch),['stdout.log','stderr.log','result.json']);
  architectures[arch]={fullBuildId:full.buildId,sdkBuildId:sdk.buildId,installedSdk:installed,starterPayloadSha256:full.payloads.SouiStarter.sha256,runtimeBase};
 }
 const webRecord=read(path.join(SDK,'out/web-sdk.json'));assert(webRecord.sourceSha256===SOURCE,'Current Web SDK binding differs');verify(SDK,webRecord.artifacts,false);
 const names=walk(path.join(SDK,'sdk/web/dist')).map(n=>'sdk/web/dist/'+n);assert(JSON.stringify(names.sort())===JSON.stringify(webRecord.artifacts.files.map(f=>f.path).sort()),'Web output set differs');
 copyFiles(path.join(SDK,'sdk/web'),path.join(INPUTS,'web-sdk'),['package.json',...walk(path.join(SDK,'sdk/web/dist')).map(n=>'dist/'+n)]);
 fs.copyFileSync(path.join(SDK,'out/web-sdk.json'),path.join(INPUTS,'records/web-sdk.json'),fs.constants.COPYFILE_EXCL);
 copyFiles(path.join(ROOT,'provenance'),path.join(INPUTS,'provenance'),walk(path.join(ROOT,'provenance')));
 const toolchainSeed=copyFiles(path.join(SDK,'tests/acceptance'),path.join(ROOT,'toolchain'),['package.json','package-lock.json']);
 const result={schema:'soui.real-renewal-preparation/v1',fixtureRevision:2,preparedAt:new Date().toISOString(),sourceRoot:SDK,sourceSha256:SOURCE,sourceInputSha256:SOURCE,sourceFiles:717,compiledCandidateCommit:'141d721910738fdcc0292532eabc7c29da2464d2',version:VERSION,parent:staging.parent,protocolParent:staging.protocolParent,inputs:snapshot(INPUTS),toolchainSeed,architectures,webSdkArtifactsSha256:webRecord.artifacts.sha256,sourceUnchanged:true,compiled:false,guiExecuted:false,realRenewalVerified:false,releaseReady:false};
 write(path.join(ROOT,'prepared.json'),result);checkSdkSource();console.log(encode({status:'current-sdk-inputs-prepared',inputFiles:result.inputs.files.length,sourceSha256:SOURCE,guiExecuted:false}));return result;
};
