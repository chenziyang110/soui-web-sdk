'use strict';
const {test}=require('node:test'); const assert=require('node:assert/strict'); const fs=require('node:fs');
const path=require('node:path'); const vm=require('node:vm'); const crypto=require('node:crypto'); const {EventEmitter}=require('node:events');
const {fixture,digest}=require('./tail-evidence-fixtures.cjs');
const source=fs.readFileSync(path.resolve(__dirname,'../../tools/internal/owned-process.cjs'),'utf8');
function harness() {
  const f=fixture(); const executable=f.context.executable, runner='F:\\runner.exe', report=f.context.report;
  const files=new Map([[runner,Buffer.from('runner')],[executable,Buffer.from('subject')]]); let readFailure=null; let argv=null;
  const fakeFs={existsSync:file=>files.has(file),readFileSync:(file,encoding)=>{
    if(file===readFailure)throw new Error('Injected context read failure');
    if(!files.has(file))throw new Error('Missing virtual file'); const bytes=files.get(file); return encoding?bytes.toString(encoding):bytes;
  },writeFileSync:(file,bytes)=>{assert.equal(files.has(file),false);files.set(file,Buffer.from(bytes));}};
  const child=new EventEmitter();child.stdin=new EventEmitter();child.stdin.writable=true;child.stdin.destroyed=false;child.stdin.end=()=>{};
  child.exitCode=null;child.signalCode=null;child.kill=()=>{}; let interval=null;
  const module={exports:{}};
  vm.runInNewContext(source,{module,exports:module.exports,Buffer,process:{platform:'win32',env:{}},
    require:name=>{
      if(name==='node:fs')return fakeFs;if(name==='node:path')return path.win32;if(name==='node:crypto')return crypto;
      if(name==='node:child_process')return {spawn:(_exe,args)=>{argv=[...args];return child;}};
      if(name==='./owned-tail-evidence.cjs')return require('../../tools/internal/owned-tail-evidence.cjs');
      throw new Error('Unexpected dependency '+name);
    },setInterval:callback=>{interval=callback;return 1;},clearInterval:()=>{},setTimeout:()=>1,clearTimeout:()=>{}});
  const args=[...f.context.args];const pin={...f.context.tailImagePin};
  const owned=module.exports.launchOwned(executable,args,{runner,cwd:f.context.cwd,report,timeoutMs:f.context.timeoutMs,drainMs:f.context.drainMs,tailImagePin:pin});
  function close(){files.set(report,f.raw());child.exitCode=124;child.emit('close',124,null);}
  return {f,files,args,pin,owned,report,argv:()=>argv,close,poll:()=>interval(),failRead:file=>{readFailure=file;}};
}
test('opt-in launch copies caller argv/pin and verifies exclusive context baseline at close',async()=>{
  const h=harness();const originalArg=h.argv()[h.argv().length-1];h.args.push('injected');h.pin.nonce='f'.repeat(32);h.pin.sha256='f'.repeat(64);
  h.close();const result=await h.owned.finished;const saved=JSON.parse(h.files.get(h.report+'.context.json'));
  assert.equal(h.argv().at(-1),originalArg);assert.equal(saved.args.includes('injected'),false);
  assert.equal(saved.tailImagePin.nonce,h.f.context.tailImagePin.nonce);assert.equal(result.passed,false);
  assert.equal(result.tailEvidence.accepted,true);assert.equal(result.contextSha256,digest(h.files.get(h.report+'.context.json')));
});
test('rewriting actual pin context cannot be washed green by a complete native stream',async()=>{
  const h=harness();h.files.set(h.report+'.context.json',Buffer.from('{}\n'));h.close();const result=await h.owned.finished;
  assert.equal(result.passed,false);assert.equal(result.tailEvidence.accepted,false);assert.match(result.reportError,/context changed/);
});
test('context read failure always resolves finished as rejected evidence',async()=>{
  const h=harness();h.failRead(h.report+'.context.json');h.close();const result=await h.owned.finished;
  assert.equal(result.tailEvidence.accepted,false);assert.match(result.reportError,/context read failure/);
});
test('ready identity failure stays rejected even after an apparently complete tail stream',async()=>{
  const h=harness();h.files.set(h.report,Buffer.from('{"event":"start","schema":"bad"}\n'));h.poll();
  await assert.rejects(h.owned.ready);h.close();const result=await h.owned.finished;
  assert.equal(result.passed,false);assert.equal(result.tailEvidence.accepted,false);assert.match(result.reportError,/start identity/);
});
