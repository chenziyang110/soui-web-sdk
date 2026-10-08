'use strict';
const fs=require('node:fs'),path=require('node:path'),assert=require('node:assert/strict');
const repo='C:/SOUI-SDK-development/preview9-tray',isolated=__dirname;
const legacy='C:/SOUI-SDK-production-validation/preview13-legacy-recovery-build-20260926';
const id=require(path.join(repo,'tools/internal/delivery-identity.cjs'));
const sdk=require(path.join(repo,'tools/sdk/soui-sdk.cjs'));
const read=p=>id.readJson(p),sha='f2ca9f2ca470c5886cda1456b31b4260795667eb4df65d4c679d8acd80f2a788';
function match(root,files){for(const f of files){const b=fs.readFileSync(id.safePath(root,f.path));assert.equal(b.length,f.bytes,f.path);assert.equal(id.hash(b),f.sha256,f.path);}}
function command(root,relative,exitCode){const directory=path.join(root,relative),record=read(path.join(directory,'result.json'));assert.equal(record.exitCode,exitCode,relative);assert.equal(record.error,null);assert.equal(record.signal,null);assert.notEqual(record.timedOut,true);for(const stream of ['stdout','stderr']){const b=fs.readFileSync(path.join(directory,stream+'.log'));assert.equal(b.length,record[stream].bytes);assert.equal(id.hash(b),record[stream].sha256);}return record;}
assert.equal(id.source(repo).sha256,sha);
const recovery=read(path.join(legacy,'result.json')),inputs=read(path.join(legacy,'inputs.json'));
const original=read(path.join(legacy,'original-before.json'));
assert.equal(id.snapshot(inputs.original,id.walk(inputs.original)).sha256,original.sha256);
assert.equal(read(path.join(legacy,'original-after.json')).sha256,original.sha256);
assert.equal(recovery.originalFiles,858);assert.equal(recovery.replacedFiles,7);
const retained=original.files.filter(f=>f.path.startsWith('.sdk-install-C4ZTbs/')).map(f=>({...f,path:f.path.slice('.sdk-install-C4ZTbs/'.length)}));
assert.equal(id.snapshot(path.join(legacy,'legacy-evidence'),id.walk(path.join(legacy,'legacy-evidence'))).sha256,id.hash(id.encoded(retained)));
for(const [name,expected] of [['automatic-recover-refused',1],['initial-verification-refused',1],['restored-verify',0],['upgrade-preview13',0],['verify-preview13',0]])command(legacy,'commands/'+name,expected);
command(legacy,'command-logs/recovery',0);
const matrices=[];
for(const [root,ambient] of [[legacy,true],[isolated,false]]){
 const matrix=read(path.join(root,'matrix-result.json')),input=read(path.join(root,'matrix-input.json')),business=read(path.join(root,'business-input.json'));
 const project=path.join(root,'project');assert.equal(matrix.matrix.length,4);assert.equal(matrix.version,'0.3.0-preview.13');assert.equal(id.source(project).sha256,input.source.sha256);match(project,business.files);
 assert.equal(sdk.readLock(project).manifest.files.length,419);
 assert.equal(id.hash(fs.readFileSync(path.join(project,'sdk-lock.json'))),input.sdkLockSha256);
 command(root,'command-logs/build-matrix',0);
 const configurations=[];
 for(const row of matrix.matrix){
  const name='build-'+row.architecture+'-'+row.configuration.toLowerCase(),directory=path.join(root,'builds',name),build=read(path.join(directory,'build.json'));
  const result=command(root,'command-logs/'+name,0);assert.equal(build.buildId,row.buildId);assert.equal(build.architecture,row.architecture);assert.equal(build.configuration,row.configuration);assert.equal(build.source.sha256,input.source.sha256);
  const payload=build.payloads.UpgradeLedger;assert.equal(payload.sha256,row.payloadSha256);assert.equal(payload.files.length,71);
  id.verifyPayload(path.join(directory,'runtime'),'UpgradeLedger',payload);
  let peFiles=0;for(const f of payload.files.filter(f=>/\.(exe|dll)$/i.test(f.path))){const b=fs.readFileSync(path.join(directory,'runtime',f.path)),offset=b.readUInt32LE(0x3c);assert.equal(b.readUInt32LE(offset),0x4550);assert.equal(b.readUInt16LE(offset+4),row.architecture==='x86'?0x14c:0x8664);++peFiles;}assert.equal(peFiles,60);
  const commands=[];
  for(const kind of ['link','CL']){
   const file=ambient?path.join(project,'out',row.architecture,'UpgradeLedger.dir',row.configuration,'UpgradeLedger.tlog',kind+'.command.1.tlog'):path.join(directory,'logs',kind+'.command.1.tlog');
   const b=fs.readFileSync(file),text=b[0]===255&&b[1]===254?b.toString('utf16le'):b.toString('utf8');
   const hasVcpkg=/[\\/]VCPKG[\\/]/i.test(text);assert.equal(hasVcpkg,ambient,kind+' '+name);
   if(kind==='link'){assert(/THIRD_PARTY[\\/]PREBUILT/i.test(text));assert(new RegExp('/MACHINE:'+row.architecture,'i').test(text));}
   commands.push({kind,file,bytes:b.length,sha256:id.hash(b),hasVcpkg});
  }
  const native=fs.readFileSync(path.join(directory,'logs','native-'+row.configuration+'.log'),'utf8');
  const warningCodes=[...new Set(native.match(/\b(?:C|LNK)\d{4}\b/g)||[])].sort();
  configurations.push({...row,peFiles,actualExitCode:result.exitCode,commands,diagnosticCodes:warningCodes});
 }
 matrices.push({root,ambientVcpkg:ambient,sourceSha256:input.source.sha256,businessFilesVerified:business.files.length,configurations});
}
const prepared=read(path.join(isolated,'preparation.json'));assert.deepEqual(prepared.childEnvironment,{VcpkgEnabled:'false',VcpkgAutoLink:'false',VcpkgEnableManifest:'false'});
const report={schema:'soui.independent-recovery-build-review/v1',reviewedAt:new Date().toISOString(),sourceSha256:sha,originalUnchanged:true,originalFiles:original.files.length,originalSha256:original.sha256,retainedLegacyFiles:retained.length,automaticLegacyRecoveryVerified:false,manualKnownIncidentRecoveryVerified:true,matrices,globalSettingsModified:false,fullDependencyRebuild:false,cleanMachineReproduction:false,guiExecuted:false,sourceDeliveryVerified:false,benchmarkValid:false,releaseReady:false};
fs.writeFileSync(path.join(isolated,'independent-review.json'),id.encoded(report),{flag:'wx'});
console.log(id.encoded({verified:true,originalFiles:858,retainedLegacyFiles:retained.length,builds:8,payloadFilesPerBuild:71,peFilesPerBuild:60,guiExecuted:false,releaseReady:false}));
