'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const {describe, readPublishedSubject, readOwnedRun, readRunFile, verifyOwnedProcess} =
  require('../../benchmarks/protocol/owned-trace-subject.cjs');

// Authored protocol fixtures test the reader's consistency rules. They are not
// native execution, ownership authentication, or benchmark measurements.
function fixture(t, {host = 'soui', scene = 'S01'} = {}) {
  const folder = fs.mkdtempSync(path.join(fs.realpathSync(os.tmpdir()), 'soui-owned-reader-'));
  t.after(() => {
    assert.equal(path.dirname(folder), fs.realpathSync(os.tmpdir()));
    fs.rmSync(folder, {recursive:true, force:true});
  });
  const root = path.join(folder, 'run'); fs.mkdirSync(root);
  const write = (name, value) => fs.writeFileSync(path.join(root, name), JSON.stringify(value) + '\n');
  fs.writeFileSync(path.join(root, 'input.ndjson'), '{}\n');
  fs.writeFileSync(path.join(root, 'process-tree.ndjson'), '{}\n');
  const record = {schema:'soui.benchmark-single-run/v2', captureRoot:root, host, scene, maxDurationMs:195000,
    dependency:host === 'soui' ? {miniblinkPath:path.join(root, 'metadata-only-node.dll')} :
      {appDirectory:path.join(root, 'metadata-only-app')},
    hostExeSha256:'a'.repeat(64), tools:{samplerSha256:'a'.repeat(64), inputSha256:'a'.repeat(64),
      ownedRunnerSha256:'b'.repeat(64)}, processes:[], ownedEvidence:[],
    traceEvidence:{input:describe(root, 'input.ndjson'), processTree:describe(root, 'process-tree.ndjson')}};
  const creation = '134000000000000001';
  for (const [index, name] of ['host', 'sampler', 'input'].entries()) {
    const pid = 73 + index, creationFileTime = (BigInt(creation) + BigInt(index)).toString();
    const args = name === 'sampler' ? ['73', '1000', '195000', path.join(root, 'process-tree.ndjson'), creation] :
      name === 'input' ? ['73', scene, path.join(root, 'input.ndjson'), 'full', creation] :
      host === 'electron' ? [record.dependency.appDirectory, '--benchmark-page=' + path.join(root, 'page-bundle/benchmark.html'),
        '--result-dir=' + path.join(root, 'page-results'), '--exit-on-result'] :
        ['--resources=' + path.join(root, 'page-bundle'), '--miniblink=' + record.dependency.miniblinkPath,
          '--result-dir=' + path.join(root, 'page-results'), '--exit-on-result'];
    const context = {schema:'soui.owned-process-context/v1', runnerSha256:'b'.repeat(64),
      executableSha256:'a'.repeat(64), executable:process.execPath,
      cwd:name === 'host' && host === 'electron' ? record.dependency.appDirectory : path.dirname(process.execPath),
      args, timeoutMs:1000, drainMs:0};
    const rows = [{schema:'soui.owned-process-run/v1', event:'start', pid, creationFileTime,
      timeoutMs:1000, drainMs:0, jobAssignedBeforeResume:true, breakawayAllowed:false},
    {event:'root-exit', elapsedMs:10, exitCode:0},
    {event:'terminal', reason:'completed', elapsedMs:10, rootExited:true, rootExitCode:0,
      activeBeforeCleanup:0, terminatedOwnedJob:false, closureConfirmed:true, activeProcesses:0,
      totalProcesses:1, limitTerminatedProcesses:0, passed:true}];
    fs.writeFileSync(path.join(root, name + '.owned.ndjson'), rows.map(row => JSON.stringify(row)).join('\n') + '\n');
    write(name + '.owned.ndjson.context.json', context);
    fs.writeFileSync(path.join(root, name + '.stdout.log'), '');
    fs.writeFileSync(path.join(root, name + '.stderr.log'), '');
    const evidence = {name, report:describe(root, name + '.owned.ndjson'),
      context:describe(root, name + '.owned.ndjson.context.json'), stdout:describe(root, name + '.stdout.log'),
      stderr:describe(root, name + '.stderr.log')};
    const result = {pid, creationFileTime, exitCode:0, supervisorExitCode:0, signal:null,
      spawnError:null, reportError:null, stopRequested:false, fallbackUsed:false, closureConfirmed:true, passed:true,
      terminal:rows.at(-1), report:path.join(root, name + '.owned.ndjson'), reportSha256:evidence.report.sha256,
      runnerSha256:context.runnerSha256, executableSha256:context.executableSha256};
    record.processes.push({name, pid, code:0, signal:null, error:null, contextSha256:evidence.context.sha256, ownedProcess:result});
    record.ownedEvidence.push(evidence);
  }
  function changeReport(name, mutate) {
    const filename = name + '.owned.ndjson';
    const rows = fs.readFileSync(path.join(root, filename), 'utf8').trimEnd().split('\n').map(JSON.parse);
    mutate(rows); fs.writeFileSync(path.join(root, filename), rows.map(row => JSON.stringify(row)).join('\n') + '\n');
    const item = record.processes.find(row => row.name === name), evidence = record.ownedEvidence.find(row => row.name === name);
    evidence.report = describe(root, filename); item.ownedProcess.reportSha256 = evidence.report.sha256;
    item.ownedProcess.terminal = rows.at(-1);
  }
  return {folder, root, record, creation, changeReport};
}

test('reader derives the native start instead of trusting an extra JSON subject and supports relocation', t => {
  const f = fixture(t); f.record.claimedSubject = {pid:999, creationFileTime:'1'};
  assert.deepEqual(readOwnedRun(f.root, f.record).subject, {pid:73, creationFileTime:f.creation});
  const relocated = path.join(f.folder, 'relocated'); fs.renameSync(f.root, relocated);
  assert.deepEqual(readOwnedRun(relocated, f.record).subject, {pid:73, creationFileTime:f.creation});
});

test('ready must match the raw native start and immutable launch context', t => {
  const f = fixture(t), item = f.record.processes[0], context = f.record.ownedEvidence[0].context;
  const expected = {contextSha256:context.sha256, runnerSha256:'b'.repeat(64), executableSha256:'a'.repeat(64),
    executable:process.execPath, cwd:path.dirname(process.execPath),
    args:JSON.parse(fs.readFileSync(path.join(f.root, context.path), 'utf8')).args};
  assert.deepEqual(readPublishedSubject(f.root, 'host', {pid:73, creationFileTime:f.creation}, expected),
    {pid:73, creationFileTime:f.creation});
  assert.throws(() => readPublishedSubject(f.root, 'host', {pid:73, creationFileTime:(BigInt(f.creation)+1n).toString()}, expected), /differs from ready/);
  fs.appendFileSync(path.join(f.root, context.path), ' ');
  assert.throws(() => readPublishedSubject(f.root, 'host', item.ownedProcess, expected), /context changed/);
});

test('valid JSON process claims cannot replace the raw PID or one-tick creation value', t => {
  for (const field of ['pid', 'creationFileTime']) {
    const f = fixture(t);
    f.record.processes[0].ownedProcess[field] = field === 'pid' ? 74 : (BigInt(f.creation)+1n).toString();
    assert.throws(() => readOwnedRun(f.root, f.record), /recorded process identity/);
  }
});

test('raw data/hash mismatches, crossed report paths and duplicate roles fail', t => {
  const mutations = [f => fs.appendFileSync(path.join(f.root, 'host.stdout.log'), 'changed'),
    f => {f.record.ownedEvidence[0].report.path = 'sampler.owned.ndjson';},
    f => {f.record.ownedEvidence[1].name = 'host';},
    f => {f.record.processes[0].ownedProcess.reportSha256 = 'f'.repeat(64);},
    f => fs.appendFileSync(path.join(f.root, 'input.ndjson'), '{}\n')];
  for (const mutate of mutations) { const f = fixture(t); mutate(f); assert.throws(() => readOwnedRun(f.root, f.record)); }
});

test('duplicate start/root-exit/terminal, invalid uint64 and missing complete newline fail even with updated hashes', t => {
  for (const mutate of [rows => rows.splice(1, 0, rows[0]), rows => rows.splice(1, 0, rows[1]),
    rows => rows.splice(1, 0, rows.at(-1)), rows => {rows[0].creationFileTime = '18446744073709551616';},
    rows => {rows[0].creationFileTime = Number('134000000000000001');},
    rows => {rows[0].creationFileTime = '01';}]) {
    const f = fixture(t); f.changeReport('host', mutate); assert.throws(() => readOwnedRun(f.root, f.record));
  }
  const f = fixture(t), filename = 'host.owned.ndjson';
  const bytes = fs.readFileSync(path.join(f.root, filename)); fs.writeFileSync(path.join(f.root, filename), bytes.subarray(0, -1));
  f.record.ownedEvidence[0].report = describe(f.root, filename);
  f.record.processes[0].ownedProcess.reportSha256 = f.record.ownedEvidence[0].report.sha256;
  assert.throws(() => readOwnedRun(f.root, f.record), /Truncated/);
});

test('a context consistently rehashed with a false collector subject still fails native provenance', t => {
  const f = fixture(t), filename = 'sampler.owned.ndjson.context.json';
  const context = JSON.parse(fs.readFileSync(path.join(f.root, filename), 'utf8'));
  context.args[4] = (BigInt(f.creation)+1n).toString(); fs.writeFileSync(path.join(f.root, filename), JSON.stringify(context)+'\n');
  f.record.ownedEvidence[1].context = describe(f.root, filename);
  f.record.processes[1].contextSha256 = f.record.ownedEvidence[1].context.sha256;
  assert.throws(() => readOwnedRun(f.root, f.record), /does not bind.*subject/);
});

test('context tool hashes, arguments, cwd and report origin cannot silently cross runs', t => {
  for (const change of [context => {context.runnerSha256 = 'f'.repeat(64);},
    context => {context.executableSha256 = 'f'.repeat(64);},
    context => {context.timeoutMs = 999;}, context => {context.args[0] = '999';},
    context => {context.cwd = path.resolve(os.tmpdir());}]) {
    const f = fixture(t), filename = 'sampler.owned.ndjson.context.json';
    const context = JSON.parse(fs.readFileSync(path.join(f.root, filename), 'utf8')); change(context);
    fs.writeFileSync(path.join(f.root, filename), JSON.stringify(context)+'\n');
    f.record.ownedEvidence[1].context = describe(f.root, filename); f.record.processes[1].contextSha256 = f.record.ownedEvidence[1].context.sha256;
    assert.throws(() => readOwnedRun(f.root, f.record));
  }
  const f = fixture(t); f.record.processes[0].ownedProcess.report = path.join(f.folder, 'elsewhere', 'host.owned.ndjson');
  assert.throws(() => readOwnedRun(f.root, f.record), /recorded process identity/);
});

test('STOP remains controlled closure with generic false and never formal natural completion', t => {
  const f = fixture(t);
  f.changeReport('host', rows => { Object.assign(rows.at(-1), {reason:'supervisor-stop', passed:false, activeBeforeCleanup:1, terminatedOwnedJob:true}); });
  const item = f.record.processes[0]; Object.assign(item.ownedProcess, {passed:false, stopRequested:true, supervisorExitCode:124}); item.error = 'supervisor-stop';
  const diagnostic = readOwnedRun(f.root, f.record, {requireNatural:false});
  assert.equal(diagnostic.controlledStopClosed, true); assert.equal(diagnostic.naturalCompletion, false);
  assert.equal(diagnostic.processes.host.genericPassed, false);
  assert.throws(() => readOwnedRun(f.root, f.record), /three naturally completed/);
  const e = f.record.ownedEvidence[0];
  for (const flag of ['fallbackUsed', 'spawnError', 'reportError']) {
    const original = item.ownedProcess[flag]; item.ownedProcess[flag] = flag === 'fallbackUsed' ? true : 'error';
    assert.throws(() => verifyOwnedProcess(f.root, 'host', item, e,
      {runnerSha256:'b'.repeat(64), executableSha256:'a'.repeat(64)})); item.ownedProcess[flag] = original;
  }
});

test('unreadable/missing files, unsafe paths, links and invalid UTF8 are rejected', t => {
  const f = fixture(t);
  for (const name of ['../outside', '/outside', 'C:/outside', 'a\\b', './host.owned.ndjson', 'missing'])
    assert.throws(() => readRunFile(f.root, name));
  const outside = path.join(f.folder, 'outside'); fs.mkdirSync(outside); fs.writeFileSync(path.join(outside, 'value'), 'secret');
  fs.symlinkSync(outside, path.join(f.root, 'junction'), process.platform === 'win32' ? 'junction' : 'dir');
  assert.throws(() => readRunFile(f.root, 'junction/value'), /Linked|escapes/);
  fs.linkSync(path.join(f.root, 'host.stdout.log'), path.join(f.root, 'alias'));
  assert.throws(() => readRunFile(f.root, 'host.stdout.log'), /unlinked/);
  fs.unlinkSync(path.join(f.root, 'alias'));
  fs.writeFileSync(path.join(f.root, 'host.owned.ndjson'), Buffer.from([255,10]));
  f.record.ownedEvidence[0].report = describe(f.root, 'host.owned.ndjson');
  f.record.processes[0].ownedProcess.reportSha256 = f.record.ownedEvidence[0].report.sha256;
  assert.throws(() => readOwnedRun(f.root, f.record), /encoding|encoded|UTF|valid/i);
});

test('v2 pair reader binds authored raw/context fixtures and cannot accept forged JSON or STOP', t => {
  const {loadRun, compare} = require('../../benchmarks/protocol/check-pair-runs.cjs');
  const {checkRunTraces} = require('../../benchmarks/protocol/check-run-traces.cjs');
  const evidence = path.resolve(__dirname,
    '../../verification/production-validation/benchmark-s02-full-preflight-2026-09-25/raw');
  const controlled = {};
  for (const host of ['soui', 'electron']) {
    const f = fixture(t, {host, scene:'S02'});
    const source = path.join(evidence, `benchmark-s02-${host}-20260925-run1`);
    const original = JSON.parse(fs.readFileSync(path.join(source, 'run.json'), 'utf8'));
    for (const name of ['page-results', 'page-bundle']) fs.cpSync(path.join(source, name), path.join(f.root, name), {recursive:true});
    const input = fs.readFileSync(path.join(source, 'input.ndjson'), 'utf8').trim().split('\n').map(JSON.parse);
    const processRows = fs.readFileSync(path.join(source, 'process-tree.ndjson'), 'utf8').trim().split('\n').map(JSON.parse);
    // These are deliberately authored protocol fixtures: the invented raw
    // identities/UTC values do not recover the historical rounded FILETIME.
    Object.assign(input[0], {schema:'soui.external-input-trace/v2', pid:73,
      processCreationTime100ns:f.creation, expectedCreationTime100ns:f.creation,
      startedAtUtcFiletime100ns:String(input[0].startedAtUtcFiletime100ns)});
    Object.assign(processRows[0], {schema:'soui.process-tree-samples/v2', rootPid:73,
      rootCreationTime100ns:f.creation, expectedCreationTime100ns:f.creation,
      startedAtUtcFiletime100ns:String(processRows[0].startedAtUtcFiletime100ns)});
    for (const row of processRows.filter(row => row.event === 'sample')) {
      row.rootPid = 73;
      row.processes = row.processes.map((member, index) => ({...member, pid:73 + index,
        creationTime100ns:index ? (BigInt(f.creation) + BigInt(index)).toString() : f.creation}));
    }
    fs.writeFileSync(path.join(f.root, 'input.ndjson'), input.map(JSON.stringify).join('\n') + '\n');
    fs.writeFileSync(path.join(f.root, 'process-tree.ndjson'), processRows.map(JSON.stringify).join('\n') + '\n');
    f.record = {...original, ...f.record, dependency:{...original.dependency, ...f.record.dependency},
      trace:checkRunTraces(input, processRows, 'S02', {ownedSubject:{pid:73, creationFileTime:f.creation}}),
      traceIdentity:{pidBound:true, creationComparison:'exact-native-filetime', exactFileTimeBound:true},
      traceEvidence:{input:describe(f.root, 'input.ndjson'), processTree:describe(f.root, 'process-tree.ndjson')}};
    fs.writeFileSync(path.join(f.root, 'run.json'), JSON.stringify(f.record) + '\n');
    controlled[host] = f;
  }
  const pair = compare(loadRun(controlled.soui.root, 'soui'), loadRun(controlled.electron.root, 'electron'));
  assert.equal(pair.schema, 'soui.benchmark-pair-preflight/v2');
  assert.equal(pair.preflightAccepted, true);
  assert.equal(pair.exactFileTimeBound, true);
  assert.equal(pair.benchmarkValid, false);
  assert.equal(pair.releaseReady, false);
  const legacy = loadRun(path.join(evidence, 'benchmark-s02-electron-20260925-run1'), 'electron');
  assert.throws(() => compare(loadRun(controlled.soui.root, 'soui'), legacy), /Mixed legacy\/exact/);
  const f = controlled.soui;
  f.record.processes[0].ownedProcess.creationFileTime = (BigInt(f.creation) + 1n).toString();
  fs.writeFileSync(path.join(f.root, 'run.json'), JSON.stringify(f.record) + '\n');
  assert.throws(() => loadRun(f.root, 'soui'), /recorded process identity/);
  f.record.processes[0].ownedProcess.creationFileTime = f.creation;
  f.changeReport('host', rows => Object.assign(rows.at(-1),
    {reason:'supervisor-stop', passed:false, activeBeforeCleanup:1, terminatedOwnedJob:true}));
  Object.assign(f.record.processes[0].ownedProcess, {passed:false, stopRequested:true, supervisorExitCode:124});
  f.record.processes[0].error = 'supervisor-stop';
  fs.writeFileSync(path.join(f.root, 'run.json'), JSON.stringify(f.record) + '\n');
  assert.throws(() => loadRun(f.root, 'soui'), /three naturally completed/);
});
