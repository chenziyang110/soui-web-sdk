'use strict';
const test=require('node:test'),assert=require('node:assert/strict'),path=require('node:path');
const verifier=require('../../tools/internal/release-verification.cjs');
const root=path.resolve(__dirname,'../..'),sha='a'.repeat(64),profile='LLVM22MSVC142';
const clone=value=>structuredClone(value),raw=file=>({path:file,bytes:123,sha256:sha});
// These are pure logical DTOs. None is a file, compiler, lease or whole-Job receipt.
function requested(){return {schema:'soui.release-requested-profile/v1',profile,
  toolsRoot:path.join(path.dirname(root),'separate-release-stock'),legacyToolsRoot:root,
  paths:{LlvmRoot:'C:/LLVM',MsvcRoot:'C:/MSVC142',WindowsSdkRoot:'C:/WindowsSdk',
    NinjaExe:'C:/Tools/ninja.exe',WindowsCmdExe:'C:/Windows/System32/cmd.exe',PowerShellExe:'C:/Windows/System32/WindowsPowerShell/v1.0/powershell.exe'},
  qualificationPassed:false,releaseReady:false};}
function step(name,version,architecture='x64',llvm=true){
  const requestFile=raw(path.join(root,'logical-phases',name,'out',architecture,'llvm-profile-request-Release.json'));
  const value={name,architecture,sdkVersion:version,sdkManifestSha256:sha,businessSha256:sha,
    build:{schema:'soui.source-build/v2',nativeBuild:'passed',version,architecture,configuration:'Release',sdkOnly:false,
      buildId:name+'-unique-build',source:{sha256:sha}},
    smoke:{schema:'soui.native-smoke/v2',version,architecture,configuration:'Release',passed:true,finishedAt:'2026-10-13T00:00:00Z',
      buildId:name+'-unique-build',sourceSha256:sha,ownedSupervision:{schema:'soui.native-smoke-owned/v1'},
      runs:[{passed:true,exitCode:0,timedOut:false,scenario:'default',log:'application-ready\nhost-close-complete\n'}]}};
  if(llvm){
    value.build.nativeToolchain={profile,requestFile,request:{discovery:{InputIdentity:sha,BridgeIdentity:'c'.repeat(64)}}};
    value.smoke.nativeToolchain={profile,requestFile,inputIdentity:sha,bridgeIdentity:'c'.repeat(64)};
    value.compilerObservation={compilerVerified:true,buildId:value.build.buildId,sourceSha256:sha,
      accepted:raw(path.join(root,name,'accepted.json')),observation:raw(path.join(root,name,'observation.json')),requestFile};
  }
  return value;
}
function aggregate(architecture='x64'){
  const version='0.5.0-preview.1',oldVersion='0.3.0-preview.10';
  const common={sdkVersion:version,rootSourceSha256:sha,sdkManifestSha256:sha,architecture,configuration:'Release',passed:true,
    nativeToolchain:profile,formalPassed:false,qualificationPassed:false,releaseReady:false,finishedAt:'2026-10-13T00:00:00Z'};
  const relocated=step('relocation',version,architecture),reports=[
    {...common,schema:'soui.generated-app-validation/v3',upgradedFrom:oldVersion,upgradeTested:true,rollbackTested:true,generatedOutsideDistribution:true,
      steps:['baseline','upgrade','rollback','reupgrade'].map((name,index)=>step(name,index%2?version:oldVersion,architecture,!!(index%2)))},
    {...common,schema:'soui.fresh-app-validation/v3',step:step('fresh',version,architecture)},
    {...common,schema:'soui.relocation/v3',step:relocated,copiedOutsideDistribution:true,buildAndNativeSmoke:true},
    {...relocated.smoke,...common}];
  return {reports,expected:{version,sourceSha256:sha,sdkManifestSha256:sha,architecture,nativeToolchain:profile}};
}
test('release LLVM input requires independent stock owner all six paths and unqualified scope',()=>{
  assert.equal(verifier.validateReleaseProfile(requested()).profile,profile);
  for(const alter of [v=>v.toolsRoot=root,v=>v.legacyToolsRoot=path.dirname(root),v=>delete v.paths.NinjaExe,
    v=>v.paths.Extra='C:/extra',v=>v.paths.LlvmRoot='relative',v=>v.paths.NinjaExe='C:/Tools/a;ninja.exe',
    v=>v.qualificationPassed=true,v=>v.releaseReady=true,v=>v.profile='MSVC143']){
    const value=requested();alter(value);assert.throws(()=>verifier.validateReleaseProfile(value));
  }
});
test('profile dispatch preserves immutable historical MSVC phases and explicit current LLVM phases',()=>{
  for(const name of ['fresh','baseline','upgrade','rollback','reupgrade','relocation']){
    assert.equal(verifier.phaseProfile({},name),'MSVC143');
    assert.equal(verifier.phaseProfile({nativeToolchain:{requested:requested()}},name),['baseline','rollback'].includes(name)?'MSVC143':profile);
  }
  assert.throws(()=>verifier.phaseProfile({nativeToolchain:{}},'invented-phase'));
});
test('explicit LLVM steps accept both architectures only with matching actual compiler smoke and build identities',()=>{
  for(const architecture of ['x86','x64']){const value=step('upgrade','0.5.0-preview.1',architecture);
    assert.doesNotThrow(()=>verifier.validateStep(value,value.sdkVersion,architecture,profile));
    assert.throws(()=>verifier.validateStep(value,value.sdkVersion,architecture));
    assert.throws(()=>verifier.validateStep(value,value.sdkVersion,architecture==='x64'?'x86':'x64',profile));
  }
});
test('Source requests cannot replace compiler observation full identities or natural ready close evidence',()=>{
  const valid=step('upgrade','0.5.0-preview.1');
  for(const alter of [v=>delete v.compilerObservation,v=>v.compilerObservation.compilerVerified=false,
    v=>v.compilerObservation.buildId='other-build',v=>v.compilerObservation.sourceSha256='b'.repeat(64),
    v=>delete v.compilerObservation.observation,v=>v.compilerObservation.requestFile={...v.compilerObservation.requestFile,sha256:'b'.repeat(64)},
    v=>v.build.nativeToolchain.requestFile={...v.build.nativeToolchain.requestFile,path:path.join(root,'another-owner/request.json')},
    v=>v.smoke.nativeToolchain.inputIdentity='b'.repeat(64),v=>v.smoke.nativeToolchain.bridgeIdentity='d'.repeat(64),
    v=>{delete v.smoke.nativeToolchain.inputIdentity;delete v.build.nativeToolchain.request.discovery.InputIdentity;},
    v=>delete v.smoke.ownedSupervision,v=>v.smoke.runs[0].timedOut=true,v=>v.smoke.runs[0].log='application-ready\n']){
    const value=clone(valid);alter(value);assert.throws(()=>verifier.validateStep(value,value.sdkVersion,'x64',profile));
  }
});
test('alternating historical and LLVM upgrade phases preserve original order business and distinct builds',()=>{
  for(const architecture of ['x86','x64']){
    const {reports,expected}=aggregate(architecture),phases=reports[0].steps;
    assert.doesNotThrow(()=>verifier.validateUpgrade(phases,reports[0].upgradedFrom,expected.version,architecture,profile));
    for(const alter of [v=>v[2].businessSha256='b'.repeat(64),v=>v.reverse(),v=>v[3].build.buildId=v[1].build.buildId,
      v=>v[0]=step('baseline',reports[0].upgradedFrom,architecture,true),v=>v[1]=step('upgrade',expected.version,architecture,false)]){
      const value=clone(phases);alter(value);assert.throws(()=>verifier.validateUpgrade(value,reports[0].upgradedFrom,expected.version,architecture,profile));
    }
  }
});
test('LLVM aggregate v3 binds selected profile both architectures and six independent phases',()=>{
  for(const architecture of ['x86','x64']){const {reports,expected}=aggregate(architecture);
    assert.doesNotThrow(()=>verifier.validateReports(reports,expected));
    assert.throws(()=>verifier.validateReports(reports,{...expected,nativeToolchain:'MSVC143'}));
    assert.throws(()=>verifier.validateReports(reports,{...expected,architecture:architecture==='x64'?'x86':'x64'}));
  }
});
test('LLVM aggregate rejects downgrade invented qualification missing actual phases and stale package Source',()=>{
  const {reports,expected}=aggregate();
  for(const alter of [v=>delete v[0].nativeToolchain,v=>v[2].nativeToolchain='MSVC143',v=>v[0].formalPassed=true,
    v=>v[1].qualificationPassed=true,v=>v[3].releaseReady=true,v=>v[0].steps.pop(),
    v=>v[0].rootSourceSha256='b'.repeat(64),v=>v[1].sdkManifestSha256='b'.repeat(64),
    v=>v[2].step.build.buildId=v[1].step.build.buildId,v=>delete v[2].step.compilerObservation]){
    const value=clone(reports);alter(value);assert.throws(()=>verifier.validateReports(value,expected));
  }
});
test('MSVC default remains independently selectable and cannot admit LLVM receipt substitution',()=>{
  const value=step('upgrade','0.5.0-preview.1','x86',false);
  assert.doesNotThrow(()=>verifier.validateStep(value,value.sdkVersion,'x86'));
  assert.throws(()=>verifier.validateStep(value,value.sdkVersion,'x86',profile));
  const unsupported=clone(value);unsupported.build.nativeToolchain={profile:'unknown'};
  assert.throws(()=>verifier.validateStep(unsupported,value.sdkVersion,'x86'));
});
