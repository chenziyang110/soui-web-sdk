'use strict';
// Execute only the two newly added platform-free policy cases. This executable
// has a console PE subsystem and does not link a product/GUI SDK target.
const fs=require('node:fs'),path=require('node:path'),crypto=require('node:crypto'),assert=require('node:assert/strict');
const pin=f=>{const s=fs.lstatSync(f);assert(s.isFile()&&!s.isSymbolicLink());const b=fs.readFileSync(f);return {path:f,bytes:b.length,sha256:crypto.createHash('sha256').update(b).digest('hex')};};
const write=(d,n,v)=>fs.writeFileSync(path.join(d,n),JSON.stringify(v,null,2)+'\n',{flag:'wx'});
function natural(r){const t=r.terminal;assert(r.passed&&r.exitCode===0&&r.supervisorExitCode===0&&r.signal===null&&r.spawnError===null&&r.reportError===null&&!r.stopRequested&&!r.fallbackUsed&&r.closureConfirmed);assert(t&&t.reason==='completed'&&t.rootExited&&t.rootExitCode===0&&t.activeBeforeCleanup===0&&t.activeProcesses===0&&!t.terminatedOwnedJob&&t.limitTerminatedProcesses===0&&t.closureConfirmed&&t.passed);}
async function main(){
  const [gate,attempt]=process.argv.slice(2);assert.equal(gate,'ROOT_REVIEWED_CURRENT_WORKBENCH_X64_POLICY_V1');assert(path.isAbsolute(attempt));assert.equal(process.platform,'win32');
  assert.equal(process.argv.length,4);
  const originalAttempt=path.join(__dirname,'attempt-ca3ff662-7003-4503-a652-00e198ce53b0');
  assert.equal(path.resolve(attempt).toLowerCase(),path.resolve(originalAttempt).toLowerCase());
  const summaryFile=path.join(attempt,'summary.json'),summary=JSON.parse(fs.readFileSync(summaryFile));
  assert.equal(pin(summaryFile).sha256,'ee2422f682d39fee1fa45ee3ea646a56975f9c3f53abb9cdc0d045ad81676746');
  assert.equal(summary.schema,'soui.workbench-consumer-build-only/v1');assert.equal(summary.passed,false);assert(summary.failure);assert.equal(summary.executableExecuted,false);assert.deepEqual(summary.verdicts.map(v=>v.architecture),['x64']);
  const caseFolder=path.join(attempt,'x64'),caseFile=path.join(caseFolder,'case-verdict.json'),buildCase=JSON.parse(fs.readFileSync(caseFile));assert.deepEqual(buildCase,summary.verdicts[0]);
  assert(buildCase.wholeJobNaturallyClosed===true&&buildCase.genericSupervisionPassed===true&&buildCase.executableExecuted===false&&buildCase.oldFailuresReclassified===false);
  const evidenceNames=['case-verdict.json','native-finished.json','native-ready.json','native.ndjson.context.json','native.ndjson','root-return.json','fixture-source-pins.json','policy-source-pins.json','policy-compile-shape.json'];
  const buildEvidence=evidenceNames.map(n=>path.join(caseFolder,n));
  const buildResult=JSON.parse(fs.readFileSync(path.join(caseFolder,'native-finished.json'))),buildReady=JSON.parse(fs.readFileSync(path.join(caseFolder,'native-ready.json'))),buildContext=JSON.parse(fs.readFileSync(path.join(caseFolder,'native.ndjson.context.json'))),buildRaw=fs.readFileSync(path.join(caseFolder,'native.ndjson'));
  require('C:/sc07/tools/internal/standard-build.cjs').verifyStream(buildRaw,buildContext,buildReady,buildResult);natural(buildResult);assert.deepEqual(buildResult.terminal,buildCase.terminal);assert.deepEqual(buildReady,buildCase.root);assert.equal(buildContext.timeoutMs,120000);assert.equal(buildContext.drainMs,2000);assert(!buildContext.tailImagePin);
  const buildReceipt=JSON.parse(fs.readFileSync(path.join(caseFolder,'root-return.json')));assert(buildReceipt.completed===true&&buildReceipt.productExecuted===false&&buildReceipt.executableExecuted===false&&buildReceipt.cleanupFailures.length===0);assert.equal(buildReceipt.rootPid,buildReady.pid);assert.equal(buildReceipt.architecture,'x64');
  const executable=buildCase.policyExecutable;assert(executable);assert.equal(buildReceipt.policyExecutable,executable.path);assert.equal(pin(executable.path).bytes,executable.bytes);assert.equal(pin(executable.path).sha256,executable.sha256);
  const compileShape=JSON.parse(fs.readFileSync(path.join(caseFolder,'policy-compile-shape.json')));assert(compileShape.consoleOnly===true&&compileShape.executed===false&&compileShape.cppTranslationUnits===1&&compileShape.units.length===1);assert.equal(compileShape.executable.sha256,executable.sha256);
  const bytes=fs.readFileSync(executable.path),offset=bytes.readUInt32LE(0x3c);assert.equal(bytes.toString('ascii',offset,offset+4),'PE\0\0');assert.equal(bytes.readUInt16LE(offset+4),0x8664);assert.equal(bytes.readUInt16LE(offset+24+68),3);
  const policyFile=path.join(__dirname,'PolicySource.actual.json'),policy=JSON.parse(fs.readFileSync(policyFile));assert.deepEqual(policy.cases,['reload-pending','runtime-pending']);assert(!policy.executed);
  const frozenFile=path.join(__dirname,'FrozenSource.actual.json'),frozen=JSON.parse(fs.readFileSync(frozenFile));assert.equal(frozen.sourceFiles.length,19);assert.equal(policy.sourceFiles.length,3);
  const buildInputFile=path.join(attempt,'inputs-before.json'),buildAfterFile=path.join(attempt,'inputs-after.json'),buildInput=JSON.parse(fs.readFileSync(buildInputFile)),buildAfter=JSON.parse(fs.readFileSync(buildAfterFile));assert.deepEqual(buildInput.sourcePins,buildAfter.sourcePins);
  const sourceProof=[];
  for(const [folderName,recordName,expectedRows]of [['source','fixture-source-pins.json',frozen.sourceFiles],['policy-source','policy-source-pins.json',policy.sourceFiles]]){
    const actualRows=JSON.parse(fs.readFileSync(path.join(caseFolder,recordName)));assert.equal(actualRows.length,expectedRows.length);const seen=new Set();
    for(const row of actualRows){const relative=path.relative(path.join(buildReceipt.workspace,folderName),row.path).split(path.sep).join('/');assert(relative&&!relative.startsWith('../')&&!path.isAbsolute(relative)&&!seen.has(relative));seen.add(relative);const expected=expectedRows.find(f=>f.relative===relative);assert(expected);for(const p of [row,expected.source,expected.copy]){const current=pin(p.path);assert.equal(current.bytes,p.bytes);assert.equal(current.sha256,p.sha256);sourceProof.push(current);}assert.equal(row.bytes,expected.copy.bytes);assert.equal(row.sha256,expected.copy.sha256);for(const p of [expected.source,expected.copy]){const frozenPin=buildInput.sourcePins.find(f=>path.resolve(f.path).toLowerCase()===path.resolve(p.path).toLowerCase());assert(frozenPin);assert.equal(frozenPin.bytes,p.bytes);assert.equal(frozenPin.sha256,p.sha256);}}
    assert.deepEqual([...seen].sort(),expectedRows.map(f=>f.relative).sort());
  }
  const runner='C:/sr06/out/benchmark-protocol/Release/OwnedProcessRunner.exe';assert.equal(pin(runner).sha256,'f254dadd4eccad3c57d69e98f260d15ee598c0b6441b7fe68fc4ef5b3b7dea83');
  const toolNames=['standard-build.cjs','owned-process.cjs','owned-tail-evidence.cjs','compiler-tail-policy.cjs'];
  const inputPins=[__filename,summaryFile,caseFile,policyFile,frozenFile,buildInputFile,buildAfterFile,...buildEvidence,...sourceProof.map(p=>p.path),executable.path,runner,process.execPath,...toolNames.map(n=>'C:/sc07/tools/internal/'+n),...policy.sourceFiles.flatMap(f=>[f.source.path,f.copy.path])].map(pin);
  const unchanged=()=>{for(const expected of inputPins)assert.deepEqual(pin(expected.path),expected);};unchanged();
  const directory=path.join(__dirname,'policy-shorts-'+crypto.randomUUID());fs.mkdirSync(directory);write(directory,'Inputs.actual.json',{inputPins,parentAttemptPassed:false,x64CasePassed:true,x86BuildStillPending:true,buildCase:pin(caseFile),sourceProof,cases:policy.cases,architecture:'x64',timeoutMs:10000,drainMs:2000,consoleSubsystem:3,unchangedNineCasesReexecuted:false,productGuiExecution:false});
  const standard=require('C:/sc07/tools/internal/standard-build.cjs'),results=[];let failure=null;
  try{for(const name of policy.cases){
    unchanged();const folder=path.join(directory,name);fs.mkdirSync(folder);
    const env={...process.env,PATH:'C:/sr06/out/x64/bin/Release/SouiWorkbench;'+(process.env.PATH||'')};
    write(folder,'Invocation.actual.json',{executable:pin(executable.path),args:[name],cwd:path.dirname(executable.path),runner:pin(runner),timeoutMs:10000,drainMs:2000,processLocalRuntimeSearchPrefix:'C:/sr06/out/x64/bin/Release/SouiWorkbench',actualModuleLoadQualificationClaimed:false,consoleOnly:true});
    const capture=await standard.capture(folder,'policy',executable.path,[name],{runner,cwd:path.dirname(executable.path),env,timeoutMs:10000,drainMs:2000});
    assert.equal(capture.readyError,null);standard.verifyStream(capture.rawReport,JSON.parse(capture.contextBytes),capture.ready,capture.result);natural(capture.result);
    const stdout=fs.readFileSync(path.join(folder,'policy-stdout.bin')),stderr=fs.readFileSync(path.join(folder,'policy-stderr.bin'));
    assert.equal(stdout.toString('utf8').replace(/\r\n/g,'\n'),'resident-shutdown-policy '+name+' passed\n');assert.equal(stderr.length,0);
    const verdict={name,ready:capture.ready,result:capture.result,passed:true,naturallyClosed:true,consoleOnly:true};write(folder,'Analysis.actual.json',verdict);results.push(verdict);unchanged();
  }}catch(error){failure={message:error.message,stack:error.stack};}
  finally{try{unchanged();}catch(error){failure||={message:error.message};}write(directory,'Summary.actual.json',{schema:'soui.current-workbench-two-policy-case-from-x64-actual/v1',parentAttemptPassed:false,x64CasePassed:true,x86BuildStillPending:true,buildCase:pin(caseFile),buildSummary:pin(summaryFile),policySource:pin(policyFile),results,failure,passed:!failure&&results.length===2,unchangedNineCasesReexecuted:false,productGuiExecution:false,currentSdk060Qualified:false,releaseReady:false});}
  console.log(JSON.stringify({directory,passed:!failure&&results.length===2}));if(failure)throw Error(failure.message);
}
main().catch(error=>{console.error(error.stack);process.exitCode=1;});
