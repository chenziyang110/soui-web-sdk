'use strict';
// Pure caller-boundary fixtures. These do not assert an actual compiler run.
const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {fixture} = require('../benchmark/tail-evidence-fixtures.cjs');
const caller = require('../../tools/internal/standard-build.cjs');
const encode = value => Buffer.from(JSON.stringify(value, null, 2) + '\n');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');

test('strict CLI rejects unknown, repeated and partially specified tail permissions', () => {
  for (const args of [['--script','tools/Test.ps1'],['--architecture','x64','--architecture','x86'],
    ['--architecture','arm64'],['--configuration','RC'],['--compiler-tail'],
    ['--tail-image','C:\\vctip.exe'],['--compiler-tail','--tail-image','C:\\vctip.exe','--tail-sha256','a'.repeat(64),'--with-gui-bindings'],
    ['--timeout-ms','1e4'],['--skip-web','false']]) assert.throws(() => caller.parseArgs(args));
  for(const flag of ['--runner','--powershell','--root'])assert.throws(()=>caller.parseArgs([flag,'relative.exe']));
  assert.throws(()=>caller.parseArgs(['--compiler-tail','--tail-image','C:\\vctip.exe','--tail-sha256','a'.repeat(64)]));
  assert.equal(caller.parseArgs(['--architecture','x86','--compiler-tail','--tail-image',fixture().context.tailImagePin.image,
    '--tail-sha256',fixture().context.tailImagePin.sha256]).compilerTail,true);
  const value = caller.parseArgs(['--architecture','x86','--configuration','Debug','--sdk-only','--skip-web']);
  assert.equal(value.architecture,'x86'); assert.equal(value.configuration,'Debug'); assert.equal(value.sdkOnly,true);
  const args = caller.buildArguments('F:\\sdk',value);
  assert.deepEqual(args,['-NoProfile','-NonInteractive','-ExecutionPolicy','Bypass','-File','F:\\sdk\\tools\\Build.ps1',
    '-Architecture','x86','-Configuration','Debug','-SdkOnly','-SkipWeb']);
});

function input(tail = false) {
  const f = fixture(); const sourceSha256 = 'c'.repeat(64), scriptSha256 = 'e'.repeat(64);
  if (!tail) {
    f.rows = [f.rows[0],{event:'root-exit',elapsedMs:100,exitCode:0},{...f.rows.at(-1),reason:'completed',passed:true,
      activeBeforeCleanup:0,terminatedOwnedJob:false,tailEvidenceComplete:false,preCleanupSnapshotSequence:0}];
    delete f.context.tailImagePin; delete f.context.report; delete f.context.runnerArgs; delete f.rows[0].snapshotNonce;
  }
  // The imported fixture's raw closure owns its original array; this fixture
  // replaces that array for the ordinary completed stream.
  f.raw=()=>Buffer.from(f.rows.map(row=>JSON.stringify(row)).join('\n')+'\n');
  const file = {path:'soui_sdk.lib',bytes:12,sha256:'d'.repeat(64)};
  const library = {files:[file],bytes:12,sha256:sha(encode([file]))};
  const artifacts = {payloads:{SouiStarter:library},optionalLibraries:{}};
  const time = Number(BigInt(f.rows[0].creationFileTime)/10000n-11644473600000n);
  const build = {schema:'soui.source-build/v2',version:'0.5.0-preview.1',buildId:'11111111-1111-4111-a111-111111111111',
    architecture:'x86',configuration:'Release',sdkOnly:false,nativeBuild:'passed',releaseReady:false,
    source:{sha256:sourceSha256},startedAt:new Date(time+100).toISOString(),finishedAt:new Date(time+200).toISOString(),...artifacts};
  const contextBytes = encode(f.context), rawReport = f.raw();
  const result = {pid:f.rows[0].pid,creationFileTime:f.rows[0].creationFileTime,exitCode:0,supervisorExitCode:tail?124:0,
    signal:null,spawnError:null,reportError:null,stopRequested:false,fallbackUsed:false,closureConfirmed:true,passed:!tail,
    terminal:f.rows.at(-1),reportSha256:sha(rawReport),runnerSha256:f.context.runnerSha256,executableSha256:f.context.executableSha256};
  if(tail) result.contextSha256=sha(contextBytes);
  const frozen = {schema:'soui.standard-build-inputs/v1',version:build.version,architecture:'x86',configuration:'Release',sdkOnly:false,
    compilerTail:tail,source:{sha256:sourceSha256},rawSource:{sha256:'7'.repeat(64)},scriptSha256,contextSha256:sha(contextBytes),context:f.context,
    tools:[{role:'compiler',path:'F:\\cl.exe',bytes:8,sha256:'1'.repeat(64)}],
    scripts:[{path:'tools/Build.ps1',bytes:8,sha256:scriptSha256}],oldBuild:{exists:true,buildId:'22222222-2222-4222-a222-222222222222'}};
  return {f,build,frozen,result,ready:{pid:result.pid,creationFileTime:result.creationFileTime},contextBytes,rawReport,
    buildReportBytes:encode(build),observed:{sourceSha256,rawSourceSha256:frozen.rawSource.sha256,scriptSha256,tools:structuredClone(frozen.tools),
      scripts:structuredClone(frozen.scripts),artifacts:structuredClone(artifacts),compilerVerified:true}};
}
function rebind(v) { v.rawReport=v.f.raw();v.result.reportSha256=sha(v.rawReport);v.result.terminal=v.f.rows.at(-1); }
test('natural completed build needs current payload and a new build identity', () => {
  const answer = caller.evaluateBuild(input());
  assert.equal(answer.accepted,true,answer.failure);assert.equal(answer.compilationCompleted,true);
  assert.equal(answer.genericSupervisionPassed,true);assert.equal(answer.compilerTailAcceptance,false);assert.equal(answer.releaseReady,false);
});
test('explicit synthetic exact-tail policy preserves generic false/124', () => {
  const v=input(true), answer=caller.evaluateBuild(v);
  assert.equal(answer.accepted,true,answer.failure);assert.equal(answer.genericSupervisionPassed,false);
  assert.equal(answer.compilerTailAcceptance,true);assert.equal(v.result.supervisorExitCode,124);assert.equal(v.result.passed,false);
});
const cases = {
  'yesterday build ID reused':v=>{v.build.buildId=v.frozen.oldBuild.buildId;v.buildReportBytes=encode(v.build);},
  'old report with identical source and new-looking ID':v=>{v.build.startedAt='2020-01-01T00:00:00.000Z';v.buildReportBytes=encode(v.build);},
  'payload current bytes differ':v=>{v.observed.artifacts.payloads.SouiStarter.files[0].sha256='f'.repeat(64);},
  'source changed during build':v=>{v.observed.sourceSha256='f'.repeat(64);},
  'raw source changes even with identical normalized identity':v=>{v.observed.rawSourceSha256='f'.repeat(64);},
  'Build script raw bytes changed':v=>{v.observed.scriptSha256='f'.repeat(64);},
  'compiler replaced without source change':v=>{v.observed.tools[0].sha256='f'.repeat(64);},
  'reviewed reader bytes changed':v=>{v.observed.scripts.push({path:'tools/internal/owned-tail-evidence.cjs',bytes:1,sha256:'f'.repeat(64)});},
  'CMake actual compiler was not observed':v=>{v.observed.compilerVerified=false;},
  'caller ready differs by one exact FILETIME tick':v=>{v.ready.creationFileTime=(BigInt(v.ready.creationFileTime)+1n).toString();},
  'valid context JSON points at another script':v=>{v.contextBytes=encode({...v.f.context,args:['-Command','true']});},
  'natural Test.ps1 cannot acquire Build policy by freezing its own context':v=>{
    v.frozen.context.args[4]='F:\\sdk\\tools\\Test.ps1';v.contextBytes=encode(v.frozen.context);v.frozen.contextSha256=sha(v.contextBytes);},
  'missing tools cannot be called unchanged':v=>{delete v.frozen.tools;delete v.observed.tools;},
  'missing raw script inputs cannot be called unchanged':v=>{delete v.frozen.scripts;delete v.observed.scripts;},
  'valid client object cannot replace native terminal':v=>{v.result.terminal={...v.result.terminal,totalProcesses:99};},
  'native report truncated':v=>{v.rawReport=v.rawReport.subarray(0,-1);v.result.reportSha256=sha(v.rawReport);},
  'duplicate native start':v=>{v.f.rows.splice(1,0,{...v.f.rows[0]});rebind(v);},
  'duplicate native root-exit':v=>{v.f.rows.splice(2,0,{event:'root-exit',exitCode:0,elapsedMs:100});rebind(v);},
  'unknown native event cannot be an invisible extra':v=>{v.f.rows.splice(1,0,{event:'unreviewed-acceptance',passed:true});rebind(v);},
  'floating member creation time cannot be rounded back into an identity':v=>{v.f.rows.splice(1,0,{event:'owned-member',pid:42,creationFileTime:134359391934583654,image:'F:\\cl.exe'});rebind(v);},
  'duplicate JSON field after valid-looking last value':v=>{v.rawReport=Buffer.from(v.rawReport.toString().replace('"event":"start"','"event":"bad","event":"start"'));v.result.reportSha256=sha(v.rawReport);},
  'unbound supervisor exit':v=>{v.result.supervisorExitCode=124;},
  'STOP cannot qualify':v=>{v.result.stopRequested=true;},
  'fallback cannot qualify':v=>{v.result.fallbackUsed=true;},
  'post-launch source missing':v=>{delete v.observed.sourceSha256;},
};
for(const [name,mutate] of Object.entries(cases)) test(name,()=>{
  const v=input();mutate(v);assert.equal(caller.evaluateBuild(v).accepted,false,name);
});
test('default tail, explicit timeout and root failure are never promoted to accepted', () => {
  const v=input(true);v.frozen.compilerTail=false;assert.equal(caller.evaluateBuild(v).accepted,false);
  for(const reason of ['timeout','supervisor-stop','root-failed']) {
    const n=input(true);n.f.rows.at(-1).reason=reason;rebind(n);assert.equal(caller.evaluateBuild(n).accepted,false,reason);
  }
});

test('workspace lease excludes both architectures and refuses foreign release', () => {
  const folder=fs.mkdtempSync(path.join(path.resolve(__dirname,'../../out'),'standard-build-lease-'));
  const lease=caller.acquireLease(folder,'x86');
  assert.throws(()=>caller.acquireLease(folder,'x86'),/lease/i);
  assert.throws(()=>caller.acquireLease(folder,'x64'),/lease/i);
  const old=fs.readFileSync(path.join(lease.directory,'owner.json'));
  fs.writeFileSync(path.join(lease.directory,'owner.json'),encode({nonce:'foreign'}));
  assert.throws(()=>lease.release(),/owner|lease/i);assert.equal(fs.existsSync(lease.directory),true);
  fs.writeFileSync(path.join(lease.directory,'owner.json'),old);lease.release();
  // Keep test artifacts in out; no recursive cleanup or guessed stale deletion.
});

test('legacy architecture lease is retained and prevents a workspace build', () => {
  const folder=fs.mkdtempSync(path.join(path.resolve(__dirname,'../../out'),'standard-build-legacy-lease-'));
  const directory=path.join(folder,'out/standard-build/lease-x86');fs.mkdirSync(directory,{recursive:true});
  const owner=encode({nonce:'historical-owner',pid:2147483647});fs.writeFileSync(path.join(directory,'owner.json'),owner);
  assert.throws(()=>caller.acquireLease(folder,'x64'),/lease/i);
  assert.equal(fs.readFileSync(path.join(directory,'owner.json')).equals(owner),true);
  assert.equal(fs.existsSync(path.join(folder,'out/standard-build/lease-workspace')),false);
});

test('configured Node records requested and resolved tool bytes', () => {
  const actual=caller.inspectTool(process.execPath);
  assert.equal(actual.path,path.resolve(process.execPath));
  assert.equal(actual.requestedPath,path.resolve(process.execPath));
  assert.equal(actual.resolvedPath,fs.realpathSync.native(process.execPath));
  assert.equal(actual.sha256,sha(fs.readFileSync(actual.resolvedPath)));
  assert.equal(actual.bytes,fs.statSync(actual.resolvedPath).size);
  assert.equal(caller.verifyTool(actual),true);
});

function junctionTool() {
  const directory=fs.mkdtempSync(path.join(path.resolve(__dirname,'../../out'),'standard-build-tool-junction-'));
  const first=path.join(directory,'first'),second=path.join(directory,'second'),junction=path.join(directory,'configured');
  fs.mkdirSync(first);fs.mkdirSync(second);
  for(const target of [first,second])fs.writeFileSync(path.join(target,'tool.exe'),Buffer.from('synthetic tool identity; never executed'));
  fs.symlinkSync(first,junction,'junction');
  return {directory,first,second,junction,requested:path.join(junction,'tool.exe')};
}

test('real private junction resolves tools but source and evidence rules still refuse links', () => {
  const f=junctionTool(),actual=caller.inspectTool(f.requested);
  assert.equal(actual.requestedPath,f.requested);
  assert.equal(actual.resolvedPath,path.join(f.first,'tool.exe'));
  assert.equal(caller.verifyTool(actual),true);
  const identity=require('../../tools/internal/delivery-identity.cjs');
  assert.throws(()=>identity.safePath(f.requested),/Linked path/);
  assert.throws(()=>identity.safePath(f.directory,'configured/tool.exe'),/Linked path/);
});

test('junction retarget to identical bytes cannot preserve frozen tool identity', () => {
  const f=junctionTool(),actual=caller.inspectTool(f.requested);
  fs.unlinkSync(f.junction);fs.symlinkSync(f.second,f.junction,'junction');
  assert.equal(sha(fs.readFileSync(f.requested)),actual.sha256);
  assert.throws(()=>caller.verifyTool(actual),/tool|identity|target/i);
  assert.equal(fs.readFileSync(path.join(f.first,'tool.exe'),'utf8'),'synthetic tool identity; never executed');
});

test('canonical tool replacement and missing junction target fail closed', () => {
  const f=junctionTool(),actual=caller.inspectTool(f.requested);
  fs.writeFileSync(path.join(f.first,'tool.exe'),Buffer.from('changed tool bytes'));
  assert.throws(()=>caller.verifyTool(actual),/tool|identity|target/i);
  fs.unlinkSync(path.join(f.first,'tool.exe'));
  assert.throws(()=>caller.verifyTool(actual));
});

test('real temporary SDK library drift fails the existing build evidence verification', () => {
  const root=fs.mkdtempSync(path.join(path.resolve(__dirname,'../../out'),'standard-build-payload-'));
  function put(name,bytes){const file=path.join(root,name);fs.mkdirSync(path.dirname(file),{recursive:true});fs.writeFileSync(file,bytes);}
  const version='0.5.0-preview.1';put('VERSION',version);
  put('sdk/include/soui/version.h','#define SOUI_SDK_VERSION_STRING "'+version+'"\n');
  put('sdk/web/package.json',encode({version}));put('sdk/web/package-lock.json',encode({version,packages:{'':{version}}}));
  put('out/sdk-x86/Release/soui_sdk.lib',Buffer.from('synthetic library bytes'));
  const build=require('../../tools/internal/build-evidence.cjs');build.start(root,'x86','Release',true);build.finish(root,'x86','Release',true);
  assert.equal(build.verifyBuild(root,'x86','Release',true).nativeBuild,'passed');
  put('out/sdk-x86/Release/soui_sdk.lib',Buffer.from('changed library bytes'));
  assert.throws(()=>build.verifyBuild(root,'x86','Release',true),/drift/i);
});
