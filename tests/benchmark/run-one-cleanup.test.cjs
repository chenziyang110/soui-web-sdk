'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {performance} = require('node:perf_hooks');
const {test} = require('node:test');
const {launch, stop, validateHostIdentity} = require('../../benchmarks/protocol/run-one.cjs');

const options = {skip: process.platform !== 'win32', timeout: 15000};
function fixture() {
  const output = path.resolve(__dirname, '../../out/benchmark-cleanup-tests');
  fs.mkdirSync(output, {recursive: true});
  return fs.mkdtempSync(path.join(output, 'attempt-'));
}
async function until(check) {
  const started = performance.now();
  while (!check()) {
    assert.ok(performance.now() - started < 5000, 'Owned fixture did not become ready');
    await new Promise(resolve => setTimeout(resolve, 20));
  }
}

test('benchmark failure cleanup proves the entire held Job closed and leaves an unrelated process alive',
  options, async t => {
    const output = fixture();
    const grandchildFile = path.join(output, 'grandchild.json');
    const parentCode = `const fs=require('node:fs');const cp=require('node:child_process');
      const child=cp.spawn(process.execPath,['-e','setInterval(()=>{},1000)'],
        {stdio:'ignore',windowsHide:true});
      fs.writeFileSync(process.argv[1],JSON.stringify({pid:child.pid,parent:process.pid}));
      setInterval(()=>{},1000);`;
    const unrelated = spawn(process.execPath, ['-e', 'setInterval(()=>{},1000)'],
      {stdio: 'ignore', windowsHide: true});
    unrelated.unref();
    t.after(() => unrelated.kill());
    const run = launch('host', process.execPath, ['-e', parentCode, grandchildFile], output, output);
    t.after(async () => { stop([run]); await run.finished; });
    const ready = await run.ready;
    await until(() => fs.existsSync(grandchildFile));
    const child = JSON.parse(fs.readFileSync(grandchildFile, 'utf8'));
    assert.equal(child.parent, ready.pid);
    const cleanup = [];
    stop([run], cleanup);
    const result = await run.finished;
    assert.deepEqual(cleanup.map(item => [item.method, item.treeCleanupVerified]),
      [['held-job-stdin', true]]);
    assert.equal(result.error, 'supervisor-stop');
    assert.equal(result.ownedProcess.passed, false);
    assert.equal(result.ownedProcess.terminal.activeProcesses, 0);
    assert.match(cleanup[0].nativeReportSha256, /^[a-f0-9]{64}$/);
    assert.throws(() => process.kill(child.pid, 0), {code: 'ESRCH'});
    assert.throws(() => process.kill(ready.pid, 0), {code: 'ESRCH'});
    assert.doesNotThrow(() => process.kill(unrelated.pid, 0));
  });

test('a zero benchmark root exit with an outstanding descendant remains a failed run',
  options, async t => {
    const output = fixture();
    const code = `const child=require('node:child_process').spawn(process.execPath,
      ['-e','setTimeout(()=>{},30000)'],{detached:true,stdio:'ignore',windowsHide:true});child.unref();`;
    const run = launch('host', process.execPath, ['-e', code], output, output);
    t.after(async () => { stop([run]); await run.finished; });
    await run.ready;
    const result = await run.finished;
    assert.equal(result.code, 0);
    assert.equal(result.error, 'owned-descendants-remain');
    assert.equal(result.ownedProcess.supervisorExitCode, 124);
    assert.equal(result.ownedProcess.passed, false);
    assert.equal(result.ownedProcess.closureConfirmed, true);
  });

test('two mutually consistent traces cannot replace the owned host identity', () => {
  const filetime = '134359302086731837';
  const processes = [{name: 'host', ownedProcess: {pid: 73, creationFileTime: filetime}}];
  const trace = {hostPid: 73, hostCreationTime100ns: Number(filetime)};
  assert.deepEqual(validateHostIdentity(trace, processes),
    {pidBound: true, creationComparison: 'legacy-json-number', exactFileTimeBound: false});
  assert.throws(() => validateHostIdentity({...trace, hostPid: 74}, processes), /owned benchmark subject/);
  assert.throws(() => validateHostIdentity({...trace, hostCreationTime100ns: 1}, processes), /owned benchmark subject/);
  assert.throws(() => validateHostIdentity(trace, []), /owned benchmark subject/);
});
