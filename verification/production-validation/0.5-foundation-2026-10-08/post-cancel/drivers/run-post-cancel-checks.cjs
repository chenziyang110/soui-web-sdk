'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {launchOwned} = require('../tools/internal/owned-process.cjs');
const id = require('../tools/internal/delivery-identity.cjs');
const evidence = require('../tools/internal/build-evidence.cjs');
const root = path.resolve(__dirname, '..');
const expected = 'e7519638abe291a21f9b03764677dd1ead8f6e27ede7a0df206de7d676477ff9';
const sha = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const original = path.join(root, 'out/benchmark-protocol/Release/OwnedProcessRunner.exe');
const runnerDir = path.join(root, 'out/validation-bootstrap', sha(original));
fs.mkdirSync(runnerDir, {recursive:true});
const runner = path.join(runnerDir, 'OwnedProcessRunner.exe');
if (!fs.existsSync(runner)) fs.copyFileSync(original, runner, fs.constants.COPYFILE_EXCL);
if (sha(runner) !== sha(original)) throw new Error('Bootstrap runner identity');
if (id.source(root).sha256 !== expected) throw new Error('Frozen production source differs');
const builds = [];
for (const architecture of ['x86','x64']) for (const configuration of ['Release','Debug']) {
  const build = evidence.verifyBuild(root, architecture, configuration);
  if (build.source.sha256 !== expected) throw new Error('Build identity differs');
  builds.push({architecture, configuration, buildId:build.buildId, sourceSha256:build.source.sha256,
    payloads:build.payloads, optionalPayloads:build.optionalPayloads});
}
const attempt = path.join(root, 'out', '0.5.0-preview.1-post-cancel-'+crypto.randomUUID());
fs.mkdirSync(attempt);
const ps = path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/powershell.exe');
const env = {...process.env,MSBUILDDISABLENODEREUSE:'1',
  PSModulePath:path.join(process.env.SystemRoot,'System32/WindowsPowerShell/v1.0/Modules')};
const steps = [{name:'unit-x64-Release',architecture:'x64',configuration:'Release',kind:'unit',timeoutMs:1200000,
  args:['-File','tools/Test.ps1','-Architecture','x64','-Configuration','Release','-UnitOnly']}];
for (const architecture of ['x86','x64']) for (const configuration of ['Release','Debug']) {
  if (architecture !== 'x64' || configuration !== 'Release') steps.push({
    name:`ctest-${architecture}-${configuration}`,architecture,configuration,kind:'ctest',timeoutMs:240000,
    args:['-Command',`& ctest --test-dir out/${architecture} -C ${configuration} --output-on-failure; exit $LASTEXITCODE`]});
  for (const kind of ['smoke','modern']) steps.push({
    name:`${kind}-${architecture}-${configuration}`,architecture,configuration,kind,timeoutMs:180000,
    args:['-File',kind==='smoke'?'tools/Test.ps1':'tools/Test-Modern.ps1',
      '-Architecture',architecture,'-Configuration',configuration,...(kind==='smoke'?['-NativeOnly']:[])]});
}
const results=[];
function save() {
  fs.writeFileSync(path.join(attempt,'results.json'),JSON.stringify({schema:'soui.post-cancel-check/v1',
    version:evidence.version(root),releaseReady:false,sourceSha256:expected,builds,
    completed:results.length===steps.length,allPassed:results.length===steps.length&&results.every(x=>x.passed),
    limitations:['Short local checks, not a frozen package or target-system acceptance.',
      'Legacy desktop scripts retain direct-child handling; enclosing Job provides supplementary process-tree evidence.'],
    results},null,2)+'\n');
}
(async()=>{
  console.log('Post-cancel attempt: '+attempt);
  for (const step of steps) {
    if (id.source(root).sha256!==expected) throw new Error('Source changed before '+step.name);
    evidence.verifyBuild(root,step.architecture,step.configuration);
    const folder=path.join(attempt,step.name);fs.mkdirSync(folder);
    const log=fs.openSync(path.join(folder,'run.log'),'wx');
    const child=launchOwned(ps,['-NoProfile','-ExecutionPolicy','Bypass',...step.args],{
      runner,cwd:root,report:path.join(folder,'owned.ndjson'),timeoutMs:step.timeoutMs,drainMs:2000,
      env,stdout:log,stderr:log});
    const result=await child.finished;fs.closeSync(log);
    const sourceUnchanged=id.source(root).sha256===expected;
    let payloadError=null;
    try {evidence.verifyBuild(root,step.architecture,step.configuration);} catch(error) {payloadError=error.message;}
    const evidenceName=step.kind==='modern'?`modern-smoke-${step.configuration}.json`:
      step.kind==='smoke'?`smoke-${step.configuration}.json`:null;
    if(evidenceName) fs.copyFileSync(path.join(root,'out',step.architecture,evidenceName),
      path.join(folder,evidenceName),fs.constants.COPYFILE_EXCL);
    if(['unit','ctest'].includes(step.kind)) {
      const nativeLog=path.join(root,'out',step.architecture,'Testing/Temporary/LastTest.log');
      if(fs.existsSync(nativeLog))fs.copyFileSync(nativeLog,path.join(folder,'LastTest.log'),fs.constants.COPYFILE_EXCL);
    }
    const checked={name:step.name,architecture:step.architecture,configuration:step.configuration,
      kind:step.kind,args:step.args,sourceUnchanged,payloadError,...result,
      passed:result.passed&&sourceUnchanged&&!payloadError};
    results.push(checked);save();
    console.log(JSON.stringify({name:step.name,passed:checked.passed,exitCode:result.exitCode,
      closureConfirmed:result.closureConfirmed,reason:result.terminal?.reason,
      totalProcesses:result.terminal?.totalProcesses,sourceUnchanged,payloadError}));
    if(!checked.passed) {
      process.exitCode=1;
      console.log(fs.readFileSync(path.join(folder,'run.log'),'utf8').slice(-4200));
    }
    if(!sourceUnchanged||payloadError) throw new Error('Source or payload drift');
  }
})().catch(error=>{console.error(error.stack);process.exitCode=1;});
