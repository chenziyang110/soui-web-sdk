'use strict';
const {test} = require('node:test');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {launch, stop} = require('../../benchmarks/protocol/run-one.cjs');
const {describe, verifyOwnedProcess} = require('../../benchmarks/protocol/owned-trace-subject.cjs');
const sampler = process.env.PROCESS_TREE_SAMPLER_EXE;
const inputTrace = process.env.BENCHMARK_INPUT_TRACE_EXE;
const runner = process.env.BENCHMARK_OWNED_RUNNER_EXE ||
  path.resolve(__dirname, '../../out/benchmark-protocol/Release/OwnedProcessRunner.exe');
const available = process.platform === 'win32' && sampler && fs.existsSync(sampler);
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
const rows = file => fs.readFileSync(file, 'utf8').trim().split('\n').map(JSON.parse);
const delay = ms => new Promise(resolve => setTimeout(resolve, ms));

function fixture(label) {
  const folder = path.resolve(__dirname, '../../out', `sampler-owned-${label}-${crypto.randomUUID()}`);
  fs.mkdirSync(folder);
  fs.writeFileSync(path.join(folder, 'inputs.json'), JSON.stringify({
    schema: 'soui.sampler-regression-inputs/v1', node: {path: process.execPath, sha256: hash(process.execPath)},
    sampler: {path: sampler, sha256: hash(sampler)}, runner: {path: runner, sha256: hash(runner)},
    ...(inputTrace ? {input: {path: inputTrace, sha256: hash(inputTrace)}} : {})}, null, 2) + '\n');
  return folder;
}
function subject(folder, orphan = false) {
  const finish = path.join(folder, 'finish');
  const leaf = path.join(folder, 'leaf.cjs'), middle = path.join(folder, 'middle.cjs'), root = path.join(folder, 'root.cjs');
  fs.writeFileSync(leaf, 'setTimeout(()=>process.exit(0),2500);\n');
  fs.writeFileSync(middle, `const {spawn}=require('node:child_process'); const fs=require('node:fs');
    const child=spawn(process.execPath,[${JSON.stringify(leaf)}],{detached:true,windowsHide:true,stdio:'ignore'});
    fs.writeFileSync(${JSON.stringify(path.join(folder, 'leaf.pid'))},String(child.pid));
    child.unref(); setTimeout(()=>process.exit(0),900);\n`);
  fs.writeFileSync(root, `const {spawn}=require('node:child_process'); const fs=require('node:fs'); const started=Date.now();
    spawn(process.execPath,${orphan ? `[${JSON.stringify(middle)}]` : "['-e','setTimeout(()=>process.exit(0),2000)']"},
      {windowsHide:true,stdio:'ignore'});
    const clock=setInterval(()=>{if(fs.existsSync(${JSON.stringify(finish)}) && Date.now()-started>=${orphan ? 2600 : 1500}) {
      clearInterval(clock); process.exit(0);}},20);
    setTimeout(()=>process.exit(9),20000).unref();\n`);
  return launch('host', process.execPath, [root], folder, folder, process.env, {runner, timeoutMs: 25000});
}
async function samplesBeforeFinish(file, count, minimumElapsed) {
  const deadline = Date.now() + 10000;
  while (Date.now() < deadline) {
    try {
      const completeLines = fs.readFileSync(file, 'utf8').split('\n').slice(0, -1).map(JSON.parse);
      const samples = completeLines.filter(row => row.event === 'sample' && row.complete);
      if (samples.length >= count && samples.at(-1).elapsedMs >= minimumElapsed) return;
    } catch (error) { if (error.code !== 'ENOENT' && !(error instanceof SyntaxError)) throw error; }
    await delay(20);
  }
  throw new Error('Sampler did not publish enough complete samples before the fixture deadline');
}
async function finishSubject(folder, host) {
  fs.writeFileSync(path.join(folder, 'finish'), 'done\n', {flag: 'wx'});
  return host.finished;
}
function assertNatural(folder, result, executable) {
  assert.equal(result.error, null, JSON.stringify(result));
  const evidence = {name: result.name, report: describe(folder, result.name + '.owned.ndjson'),
    context: describe(folder, result.name + '.owned.ndjson.context.json'),
    stdout: describe(folder, result.name + '.stdout.log'), stderr: describe(folder, result.name + '.stderr.log')};
  const verified = verifyOwnedProcess(folder, result.name, result, evidence,
    {runnerSha256: hash(runner), executableSha256: hash(executable)});
  assert.equal(verified.naturalCompletion, true);
  assert.equal(verified.controlledStopClosed, false);
}
async function cleanup(runs) {
  // Only this fixture's held Job control channel can terminate its subjects.
  stop(runs);
  await Promise.all(runs.map(run => run.finished));
}

test('supervised sampler binds the real subject and retains descendants after reparenting',
  {skip: !available, timeout: 30000}, async () => {
    assert.ok(fs.existsSync(runner), 'A compiled owned runner is required');
    for (const orphan of [false, true]) {
      const folder = fixture(orphan ? 'orphan' : 'normal'), runs = [];
      try {
        const host = subject(folder, orphan); runs.push(host);
        const ready = await host.ready, file = path.join(folder, 'process-tree.ndjson');
        const collector = launch('sampler', sampler, [String(ready.pid), orphan ? '50' : '100', '10000', file, ready.creationFileTime],
          path.dirname(sampler), folder, process.env, {runner, timeoutMs: 15000}); runs.push(collector);
        await collector.ready;
        await samplesBeforeFinish(file, 8, orphan ? 1600 : 1200);
        const [hostResult, collectorResult] = await Promise.all([finishSubject(folder, host), collector.finished]);
        assertNatural(folder, hostResult, process.execPath); assertNatural(folder, collectorResult, sampler);
        const actual = rows(file), samples = actual.filter(row => row.event === 'sample');
        assert.equal(actual[0].schema, 'soui.process-tree-samples/v2');
        assert.equal(actual[0].rootPid, ready.pid);
        assert.equal(actual[0].rootCreationTime100ns, ready.creationFileTime);
        assert.equal(actual[0].expectedCreationTime100ns, ready.creationFileTime);
        assert.ok(samples.length >= 8); assert.ok(samples.every(row => row.complete));
        assert.ok(samples.some(row => row.processes.length >= 2));
        assert.ok(samples.some(row => Number.isFinite(row.cpuPercent)));
        for (const sample of samples) {
          for (const member of sample.processes) assert.match(member.creationTime100ns, /^[1-9][0-9]*$/);
          assert.equal(sample.processes.find(member => member.pid === ready.pid).creationTime100ns, ready.creationFileTime);
        }
        assert.equal(actual.at(-1).rootExitCode, 0); assert.equal(actual.at(-1).durationExpired, false);
        if (orphan) {
          const leafPid = Number(fs.readFileSync(path.join(folder, 'leaf.pid'), 'utf8'));
          const seen = samples.filter(row => row.processes.some(member => member.pid === leafPid)).map(row => row.elapsedMs);
          assert.ok(seen.some(elapsed => elapsed > 1200), `Detached leaf disappeared after parent exit: ${seen}`);
        }
        fs.writeFileSync(path.join(folder, 'result.json'), JSON.stringify({ready, hostResult, collectorResult}, null, 2) + '\n');
      } finally { await cleanup(runs); }
    }
  });
test('supervised sampler timeout remains a failure with a closed owned Job',
  {skip: !available, timeout: 15000}, async () => {
    const folder = fixture('timeout'), runs = [];
    try {
      const host = subject(folder); runs.push(host);
      const ready = await host.ready, file = path.join(folder, 'process-tree.ndjson');
      const collector = launch('sampler', sampler, [String(ready.pid), '100', '300', file, ready.creationFileTime],
        path.dirname(sampler), folder, process.env, {runner, timeoutMs: 5000}); runs.push(collector);
      const failed = await collector.finished;
      assert.equal(failed.code, 2); assert.equal(failed.ownedProcess.passed, false);
      assert.equal(failed.ownedProcess.closureConfirmed, true);
      assert.equal(rows(file).at(-1).durationExpired, true);
      const hostResult = await finishSubject(folder, host); assertNatural(folder, hostResult, process.execPath);
      fs.writeFileSync(path.join(folder, 'result.json'), JSON.stringify({ready, hostResult, failed}, null, 2) + '\n');
    } finally { await cleanup(runs); }
  });
test('both real collectors reject a one-tick mismatch before output creation; sampler preserves existing output',
  {skip: !available, timeout: 15000}, async () => {
    const folder = fixture('reject'), runs = [];
    try {
      const host = subject(folder); runs.push(host);
      const ready = await host.ready, wrong = (BigInt(ready.creationFileTime) + 1n).toString(), results = [];
      for (const [name, executable] of [['sampler', sampler], ...(inputTrace ? [['input', inputTrace]] : [])]) {
        const file = path.join(folder, name + '-wrong.ndjson');
        const args = name === 'sampler' ? [String(ready.pid), '100', '300', file, wrong] :
          [String(ready.pid), 'S01', file, 'full', wrong];
        const run = launch(name, executable, args, path.dirname(executable), folder, process.env,
          {runner, timeoutMs: 5000}); runs.push(run);
        const result = await run.finished;
        assert.equal(result.code, 1, JSON.stringify(result)); assert.equal(result.ownedProcess.passed, false);
        assert.equal(result.ownedProcess.closureConfirmed, true);
        assert.match(fs.readFileSync(path.join(folder, name + '.stderr.log'), 'utf8'), /identity differs/);
        assert.equal(fs.existsSync(file), false); results.push(result);
      }
      // A separate output directory avoids replacing any completed native report.
      const duplicateFolder = path.join(folder, 'duplicate'); fs.mkdirSync(duplicateFolder);
      const existing = path.join(duplicateFolder, 'existing.ndjson'); fs.writeFileSync(existing, 'original bytes\n', {flag: 'wx'});
      const duplicate = launch('sampler', sampler, [String(ready.pid), '100', '300', existing, ready.creationFileTime],
        path.dirname(sampler), duplicateFolder, process.env, {runner, timeoutMs: 5000}); runs.push(duplicate);
      const duplicateResult = await duplicate.finished;
      assert.equal(duplicateResult.code, 1); assert.equal(duplicateResult.ownedProcess.passed, false);
      assert.equal(duplicateResult.ownedProcess.closureConfirmed, true);
      assert.equal(fs.readFileSync(existing, 'utf8'), 'original bytes\n');
      const hostResult = await finishSubject(folder, host); assertNatural(folder, hostResult, process.execPath);
      fs.writeFileSync(path.join(folder, 'result.json'), JSON.stringify({ready, hostResult, results, duplicateResult}, null, 2) + '\n');
    } finally { await cleanup(runs); }
  });
