'use strict';
const {test}=require('node:test'); const assert=require('node:assert/strict'); const fs=require('node:fs');
const path=require('node:path');const crypto=require('node:crypto');
const {launchOwned}=require('../../tools/internal/owned-process.cjs');
const {readTailEvidence}=require('../../tools/internal/owned-tail-evidence.cjs');
const {acceptCompilerTail}=require('../../tools/internal/compiler-tail-policy.cjs');
const runner=process.env.SOUI_TAIL_RUNNER,fixture=process.env.SOUI_TAIL_FIXTURE;
const enabled=process.platform==='win32'&&!!runner&&!!fixture&&!!process.env.SOUI_TAIL_JS_EVIDENCE_DIRECTORY;
const sha=file=>crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
function directory() {
  const base=process.env.SOUI_TAIL_JS_EVIDENCE_DIRECTORY;fs.mkdirSync(base,{recursive:true});const folder=fs.mkdtempSync(path.join(base,'attempt-'));
  fs.writeFileSync(path.join(folder,'inputs.json'),JSON.stringify({schema:'soui.tail-js-native-inputs/v1',runner,runnerSha256:sha(runner),
    fixture,fixtureSha256:sha(fixture),testSha256:sha(__filename),noGui:true,actualCompilerTail:false},null,2)+'\n');return folder;
}
for(const mode of ['single','unknown','multi','brief','context-rewrite','pin-setup-failure'])test('actual '+mode+' opt-in client/native linkage',{skip:!enabled},async t=>{
  const folder=directory();const pin=path.join(folder,'pinned-helper.exe');fs.copyFileSync(fixture,pin,fs.constants.COPYFILE_EXCL);
  let helper=pin;
  if(mode==='unknown'){helper=path.join(folder,'unknown-copy.exe');fs.copyFileSync(fixture,helper,fs.constants.COPYFILE_EXCL);assert.equal(sha(helper),sha(pin));}
  const actualMode=['context-rewrite','pin-setup-failure'].includes(mode)?'single':mode;
  const args=actualMode==='brief'?['brief']:[actualMode,folder,helper];
  const report=path.join(folder,'owned.ndjson');
  const owned=launchOwned(fixture,args,{runner,cwd:folder,report,timeoutMs:8000,drainMs:100,stdout:'pipe',stderr:'pipe',
    tailImagePin:{image:pin,sha256:mode==='pin-setup-failure'?'f'.repeat(64):sha(pin),nonce:crypto.randomBytes(16).toString('hex')}});
  const stdout=[],stderr=[];owned.child.stdout.on('data',b=>stdout.push(b));owned.child.stderr.on('data',b=>stderr.push(b));let done=false;
  t.after(async()=>{if(!done){owned.stop();await owned.finished;}});
  if(mode==='context-rewrite') {await owned.ready;fs.writeFileSync(report+'.context.json','{}\n');}
  const result=await owned.finished;done=true;
  fs.writeFileSync(path.join(folder,'stdout.bin'),Buffer.concat(stdout));fs.writeFileSync(path.join(folder,'stderr.bin'),Buffer.concat(stderr));
  fs.writeFileSync(path.join(folder,'finished.json'),JSON.stringify(result,null,2)+'\n');
  assert.equal(result.reportSha256,sha(report));assert.equal(acceptCompilerTail({result}).compilerTailAcceptance,false,'A helper fixture is not a reviewed MSVC Build.ps1 invocation');
  if(mode==='pin-setup-failure') {await assert.rejects(owned.ready);assert.equal(result.pid,null);assert.equal(result.supervisorExitCode,125);assert.equal(result.tailEvidence.accepted,false);return;}
  assert.equal(result.closureConfirmed,true);
  if(mode==='brief') {assert.equal(result.passed,true);assert.equal(result.tailEvidence.accepted,false);return;}
  assert.equal(result.supervisorExitCode,124);assert.equal(result.passed,false);assert.equal(result.terminal.reason,'owned-descendants-remain');
  if(mode==='single') {
    assert.equal(result.tailEvidence.accepted,true,result.tailEvidence.failure);
    const context=JSON.parse(fs.readFileSync(report+'.context.json'));
    assert.deepEqual(result.tailEvidence,readTailEvidence(fs.readFileSync(report),{context,reportSha256:result.reportSha256}));
    assert.equal(result.contextSha256,sha(report+'.context.json'));
  }else {assert.equal(result.tailEvidence.accepted,false);if(mode==='context-rewrite')assert.match(result.reportError,/context changed/);}
});
