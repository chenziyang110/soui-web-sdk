'use strict';
const {test}=require('node:test'); const assert=require('node:assert/strict');
const {acceptCompilerTail}=require('../../tools/internal/compiler-tail-policy.cjs');
const {fixture,digest,encode}=require('./tail-evidence-fixtures.cjs');
function input() {
  const value=fixture(); const rawReport=value.raw(); const context=value.context; const contextBytes=encode(context);
  const files=[{path:'soui_sdk.lib',bytes:12,sha256:'d'.repeat(64)}];
  const library={sha256:digest(Buffer.from(JSON.stringify(files,null,2)+'\n')),bytes:12,files};
  const payloads={SouiStarter:library}; const artifacts={payloads,optionalLibraries:{}};
  const sourceSha256='c'.repeat(64); const scriptSha256='e'.repeat(64);
  const time=new Date(Number(BigInt(value.rows[0].creationFileTime)/10000n-11644473600000n));
  const build={schema:'soui.source-build/v2',nativeBuild:'passed',releaseReady:false,buildId:'11111111-1111-4111-a111-111111111111',
    version:'0.5.0-preview.1',architecture:'x86',configuration:'Release',sdkOnly:false,source:{sha256:sourceSha256},
    startedAt:new Date(+time+100).toISOString(),finishedAt:new Date(+time+200).toISOString(),...artifacts};
  const buildReportBytes=encode(build);
  const reviewedInvocation={schema:'soui.reviewed-compiler-invocation/v1',executable:context.executable,args:[...context.args],cwd:context.cwd,
    scriptSha256,sourceSha256,buildId:build.buildId,version:build.version,architecture:build.architecture,configuration:build.configuration,
    sdkOnly:false,artifacts:structuredClone(artifacts),runnerSha256:context.runnerSha256,executableSha256:context.executableSha256,
    contextSha256:digest(contextBytes),reportSha256:digest(rawReport),buildReportSha256:digest(buildReportBytes)};
  const result={pid:value.rows[0].pid,creationFileTime:value.rows[0].creationFileTime,exitCode:0,supervisorExitCode:124,
    signal:null,spawnError:null,reportError:null,stopRequested:false,fallbackUsed:false,closureConfirmed:true,passed:false,
    terminal:value.rows.at(-1),reportSha256:digest(rawReport),contextSha256:digest(contextBytes),
    runnerSha256:context.runnerSha256,executableSha256:context.executableSha256};
  return {value,build,reviewedInvocation,context,contextBytes,rawReport,buildReportBytes,result,
    actualScriptSha256:scriptSha256,currentSourceSha256:sourceSha256,currentArtifacts:structuredClone(artifacts)};
}
function rebindRaw(value) {
  value.rawReport=value.value.raw(); value.reviewedInvocation.reportSha256=digest(value.rawReport); value.result.reportSha256=value.reviewedInvocation.reportSha256;
}
for (const version of ['0.5.0-rc.1', '0.5.0']) test('compiler continuation preserves false supervision for ' + version, () => {
  const v = input();
  v.build.version = v.reviewedInvocation.version = version;
  v.buildReportBytes = encode(v.build);
  v.reviewedInvocation.buildReportSha256 = digest(v.buildReportBytes);
  const result = acceptCompilerTail(v);
  assert.equal(result.compilerTailAcceptance, true, result.failure);
  assert.equal(result.genericSupervisionPassed, false);
  assert.equal(v.result.passed, false);
  assert.equal(v.result.supervisorExitCode, 124);
  v.build.version = v.reviewedInvocation.version = '0.5.0-beta.1';
  v.buildReportBytes = encode(v.build);
  v.reviewedInvocation.buildReportSha256 = digest(v.buildReportBytes);
  assert.equal(acceptCompilerTail(v).compilerTailAcceptance, false);
});

test('explicit synthetic reviewed build keeps completed/generic/compiler exception separate',()=>{
  const result=acceptCompilerTail(input());
  assert.equal(result.compilationCompleted,true); assert.equal(result.genericSupervisionPassed,false);
  assert.equal(result.compilerTailAcceptance,true); assert.equal(result.diskBytesOnly,true);
});
const mutations={
  'missing reviewed invocation': v=>{delete v.reviewedInvocation;},
  'missing freshly observed script hash': v=>{delete v.actualScriptSha256;},
  'missing freshly observed source hash': v=>{delete v.currentSourceSha256;},
  'missing freshly observed artifacts': v=>{delete v.currentArtifacts;},
  'changed actual Build script': v=>{v.actualScriptSha256='f'.repeat(64);},
  'changed current source': v=>{v.currentSourceSha256='f'.repeat(64);},
  'changed current payload': v=>{v.currentArtifacts.payloads.SouiStarter.files[0].sha256='f'.repeat(64);},
  'old completed buildId': v=>{v.build.buildId='22222222-2222-4222-a222-222222222222';v.buildReportBytes=encode(v.build);v.reviewedInvocation.buildReportSha256=digest(v.buildReportBytes);},
  'old identical-source build timestamp': v=>{v.build.startedAt='2020-01-01T00:00:00.000Z';v.buildReportBytes=encode(v.build);v.reviewedInvocation.buildReportSha256=digest(v.buildReportBytes);},
  'unbound raw report bytes': v=>{v.rawReport=Buffer.concat([v.rawReport,Buffer.from('\n')]);},
  'unbound context bytes': v=>{v.contextBytes=encode({...v.context,drainMs:900});},
  'different published root FILETIME': v=>{v.result.creationFileTime='134359391934214889';},
  'client ready/report failure': v=>{v.result.reportError='Ready identity failed';},
  'client STOP even complete native stream': v=>{v.result.stopRequested=true;},
  'fallback held supervisor kill': v=>{v.result.fallbackUsed=true;},
  'spawn error': v=>{v.result.spawnError='spawn failed';},
  'missing cleanup confirmation': v=>{v.result.closureConfirmed=false;},
  'wrong generic result laundering': v=>{v.result.passed=true;},
  'supervisor wrong exitcode': v=>{v.result.supervisorExitCode=0;},
  'script invocation via Command is rejected': v=>{v.context.args[3]='-Command';v.reviewedInvocation.args=[...v.context.args];v.contextBytes=encode(v.context);v.reviewedInvocation.contextSha256=digest(v.contextBytes);v.result.contextSha256=v.reviewedInvocation.contextSha256;},
  'test script never inherits compiler policy': v=>{v.context.args[4]='F:\\sdk\\tools\\Test.ps1';v.reviewedInvocation.args=[...v.context.args];v.contextBytes=encode(v.context);v.reviewedInvocation.contextSha256=digest(v.contextBytes);v.result.contextSha256=v.reviewedInvocation.contextSha256;},
  'unreviewed GUI executable rejected': v=>{v.context.executable='F:\\sdk\\out\\SouiStarter.exe';v.reviewedInvocation.executable=v.context.executable;},
  'configuration argv differs': v=>{v.context.args[8]='Debug';},
  'unknown invocation option': v=>{v.context.args.push('-Command','arbitrary');v.reviewedInvocation.args=[...v.context.args];},
  'Build native pending': v=>{v.build.nativeBuild='pending';v.buildReportBytes=encode(v.build);v.reviewedInvocation.buildReportSha256=digest(v.buildReportBytes);},
  'Build artifact manifest invalid': v=>{v.build.payloads.SouiStarter.sha256='f'.repeat(64);v.buildReportBytes=encode(v.build);v.reviewedInvocation.buildReportSha256=digest(v.buildReportBytes);},
  'Hostx86 same basename is rejected': v=>{v.context.tailImagePin.image=v.context.tailImagePin.image.replace('Hostx64','Hostx86');v.contextBytes=encode(v.context);v.reviewedInvocation.contextSha256=digest(v.contextBytes);v.result.contextSha256=v.reviewedInvocation.contextSha256;},
  'target x64 cannot infer x86 target pin': v=>{v.reviewedInvocation.architecture='x64';},
  'old unpinned exact-looking stream rejected': v=>{delete v.context.tailImagePin;v.contextBytes=encode(v.context);v.reviewedInvocation.contextSha256=digest(v.contextBytes);v.result.contextSha256=v.reviewedInvocation.contextSha256;},
  'STOP terminal cannot qualify': v=>{v.value.rows.at(-1).reason='supervisor-stop';rebindRaw(v);},
  'timeout terminal cannot qualify': v=>{v.value.rows.at(-1).reason='timeout';rebindRaw(v);},
  'historical unknown enumeration remains rejected': v=>{v.value.rows.splice(2,0,{event:'member-unavailable',pid:42,win32Error:5});rebindRaw(v);},
};
for(const [name,mutate]of Object.entries(mutations))test(name,()=>{const v=input();mutate(v);assert.equal(acceptCompilerTail(v).compilerTailAcceptance,false);});
test('valid generic completed build needs no tail exception',()=>{
  const v=input();v.value.rows.at(-1).reason='completed';v.value.rows.at(-1).passed=true;v.result.passed=true;v.result.supervisorExitCode=0;
  v.value.rows.at(-1).activeBeforeCleanup=0;v.value.rows.at(-1).terminatedOwnedJob=false;v.value.rows.at(-1).tailEvidenceComplete=false;
  rebindRaw(v);const result=acceptCompilerTail(v);
  assert.equal(result.compilationCompleted,true);assert.equal(result.genericSupervisionPassed,true);assert.equal(result.compilerTailAcceptance,false);
});
test('MSVC Hostx64/x64 exact configured object uses its independently measured size/hash',()=>{
  const v=input(); const allowedSHA='651345513aae26090c813b1ded859a5680c3823ca716a80e1d337bac0ca797ef';
  v.reviewedInvocation.architecture=v.build.architecture='x64';v.context.args[6]='x64';v.reviewedInvocation.args=[...v.context.args];
  v.context.tailImagePin.image=v.context.tailImagePin.image.replace('\\x86\\','\\x64\\');v.context.tailImagePin.sha256=allowedSHA;
  v.context.runnerArgs=['--timeout-ms','10000','--drain-ms','100','--stop-on-stdin','--report',v.context.report,'--cwd',v.context.cwd,
    '--pin-tail-image',v.context.tailImagePin.image,'--pin-tail-sha256',allowedSHA,'--snapshot-nonce',v.context.tailImagePin.nonce,'--',v.context.executable,...v.context.args];
  for(const row of v.value.rows)for(const obj of [row,row.diskObject].filter(Boolean)) {
    for(const field of ['canonicalPath','volumePath','image'])if(typeof obj[field]==='string')obj[field]=obj[field].replace('\\x86\\','\\x64\\');
    if(obj.sha256){obj.sha256=allowedSHA;obj.size='335520';}
  }
  v.contextBytes=encode(v.context);v.reviewedInvocation.contextSha256=digest(v.contextBytes);v.result.contextSha256=v.reviewedInvocation.contextSha256;
  v.buildReportBytes=encode(v.build);v.reviewedInvocation.buildReportSha256=digest(v.buildReportBytes);rebindRaw(v);
  assert.equal(acceptCompilerTail(v).compilerTailAcceptance,true);
});
