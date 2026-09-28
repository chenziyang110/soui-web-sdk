'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawn} = require('node:child_process');
const {test} = require('node:test');
const {isProhibitedProcessName, parseTasklistCsv, classifyLiveProcesses,
  liveInterferenceCheck, monitorChild} = require('../../benchmarks/protocol/run-series.cjs');

test('preflight process list rejects missing or malformed tasklist rows', () => {
  assert.deepEqual(parseTasklistCsv('"System Idle Process","0","Services","0","8 K"\r\n'
    + '"qemu-system-x86_64.exe","42","Console","1","1 K"\r\n'),
    [{name: 'qemu-system-x86_64.exe', pid: 42}]);
  for (const output of ['', 'INFO: No tasks are running',
    '"System Idle Process","0","Services","0","8 K"',
    '"notepad.exe","10","Console","1","1 K"\r\nINVALID',
    '"notepad.exe","0","Console","1","1 K"',
    '"qemu-system-x86_64.exe","0","Console","1","1 K"'])
    assert.throws(() => parseTasklistCsv(output), /Tasklist returned/);
});

test('preflight and live gate share the full SOUI workload classification', () => {
  for (const name of ['qemu-system-x86_64.exe', 'qemu-system-x86_64w.exe',
    'qemu-system-aarch64.exe', 'Electron.exe',
    'soui_bridge_tests.exe', 'SouiWorkbench.exe', 'cl.exe',
    'ProcessTreeSampler.exe', 'InputTrace.exe'])
    assert.equal(isProhibitedProcessName(name), true, name);
  assert.equal(isProhibitedProcessName('notepad.exe'), false);
});

test('live gate excludes the benchmark run and its descendants while finding external load', () => {
  const result = classifyLiveProcesses([
    {pid: 100, parentPid: 1, name: 'node.exe', createdAt: 'a'},
    {pid: 200, parentPid: 100, name: 'electron.exe', createdAt: 'b'},
    {pid: 300, parentPid: 200, name: 'electron.exe', createdAt: 'c'},
    {pid: 400, parentPid: 1, name: 'qemu-system-x86_64.exe', createdAt: 'd'},
    {pid: 500, parentPid: 1, name: 'cl.exe', createdAt: 'e'},
  ], 100, new Map());
  assert.deepEqual([...result.owned.keys()].sort((a, b) => a - b), [100, 200, 300]);
  assert.deepEqual(result.competingProcesses.map(process => process.pid), [400, 500]);
  assert.equal(result.rootPresent, true);
});

test('live gate rejects infrastructure started inside its own process tree', () => {
  const result = classifyLiveProcesses([
    {pid: 100, parentPid: 1, name: 'node.exe', createdAt: 'a'},
    {pid: 200, parentPid: 100, name: 'SouiBenchmark.exe', createdAt: 'b'},
    {pid: 300, parentPid: 100, name: 'ProcessTreeSampler.exe', createdAt: 'c'},
    {pid: 400, parentPid: 200, name: 'qemu-system-x86_64.exe', createdAt: 'd'},
    {pid: 500, parentPid: 100, name: 'cl.exe', createdAt: 'e'},
  ], 100, new Map());
  assert.deepEqual(result.competingProcesses.map(process => process.pid), [400, 500]);
});

test('live gate remembers observed child identity after its parent exits', () => {
  const result = classifyLiveProcesses([
    {pid: 100, parentPid: 1, name: 'node.exe', createdAt: 'a'},
    {pid: 300, parentPid: 200, name: 'electron.exe', createdAt: 'c'},
    {pid: 400, parentPid: 1, name: 'electron.exe', createdAt: 'd'},
  ], 100, new Map([[100, 'a'], [200, 'b'], [300, 'c']]));
  assert(result.owned.has(300));
  assert.deepEqual(result.competingProcesses.map(process => process.pid), [400]);
});

test('live gate does not trust a new child of an absent parent PID', () => {
  const result = classifyLiveProcesses([
    {pid: 100, parentPid: 1, name: 'node.exe', createdAt: 'a'},
    {pid: 300, parentPid: 200, name: 'electron.exe', createdAt: 'c'},
  ], 100, new Map([[100, 'a'], [200, 'b']]));
  assert.equal(result.owned.has(300), false);
  assert.deepEqual(result.competingProcesses.map(process => process.pid), [300]);
});

test('live gate records when the runner has disappeared', () => {
  const result = classifyLiveProcesses([
    {pid: 200, parentPid: 100, name: 'electron.exe', createdAt: 'b'},
  ], 100, new Map([[100, 'a'], [200, 'b']]));
  assert.equal(result.rootPresent, false);
  assert.deepEqual(result.competingProcesses, []);
});

test('live gate treats a benchmark host outside the owned tree as interference', () => {
  const result = classifyLiveProcesses([
    {pid: 100, parentPid: 1, name: 'node.exe', createdAt: 'a'},
    {pid: 200, parentPid: 100, name: 'SouiBenchmark.exe', createdAt: 'b'},
    {pid: 300, parentPid: 50, name: 'SouiBenchmark.exe', createdAt: 'c'},
  ], 100, new Map());
  assert.deepEqual(result.competingProcesses.map(process => process.pid), [300]);
});

test('live gate catches other SOUI tests and build drivers', () => {
  const result = classifyLiveProcesses([
    {pid: 100, parentPid: 1, name: 'node.exe', createdAt: 'a'},
    {pid: 200, parentPid: 1, name: 'soui_bridge_tests.exe', createdAt: 'b'},
    {pid: 300, parentPid: 1, name: 'cmake.exe', createdAt: 'c'},
    {pid: 400, parentPid: 1, name: 'ninja.exe', createdAt: 'd'},
  ], 100, new Map());
  assert.deepEqual(result.competingProcesses.map(process => process.pid), [200, 300, 400]);
});

test('live gate rejects a reused PID with a different creation identity', () => {
  const result = classifyLiveProcesses([
    {pid: 100, parentPid: 1, name: 'node.exe', createdAt: 'a'},
    {pid: 200, parentPid: 1, name: 'electron.exe', createdAt: 'new'},
  ], 100, new Map([[100, 'a'], [200, 'old']]));
  assert.deepEqual(result.competingProcesses.map(process => process.pid), [200]);
});

test('live gate fails closed if the runner PID changes identity', () => {
  assert.throws(() => classifyLiveProcesses([
    {pid: 100, parentPid: 1, name: 'node.exe', createdAt: 'new'},
  ], 100, new Map([[100, 'old']])), /identity changed/);
});

test('Windows live gate obtains process identities with a fail-closed query',
  {skip: process.platform !== 'win32'}, () => {
    const result = liveInterferenceCheck(process.pid, new Map());
    assert.match(result.checkedAt, /^\d{4}-\d{2}-\d{2}T/);
    assert(result.owned.has(process.pid));
    assert(Array.isArray(result.competingProcesses));
    for (const process of result.competingProcesses) {
      assert(Number.isSafeInteger(process.pid) && process.pid > 0);
      assert.equal(typeof process.name, 'string');
    }
  });

test('live gate stops only its own runner when external workload appears',
  {skip: process.platform !== 'win32'}, async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-series-gate-'));
    const gateFile = path.join(root, 'gates.ndjson');
    fs.writeFileSync(gateFile, '');
    const child = spawn(process.execPath, ['-e', 'setInterval(() => {}, 1000)'],
      {stdio: 'ignore', windowsHide: true});
    try {
      await assert.rejects(monitorChild(child, gateFile, 1, 'soui', pid => ({
        checkedAt: new Date().toISOString(), owned: new Map([[pid, 'owned']]),
        rootPresent: true,
        competingProcesses: [{pid: 98765, name: 'qemu-system-x86_64.exe'}],
      }), 5000), /External workload appeared/);
      const rows = fs.readFileSync(gateFile, 'utf8').trim().split('\n').map(JSON.parse);
      assert.equal(rows[0].competingProcesses[0].pid, 98765);
      assert.equal(rows[1].cleanupReason, 'external-workload');
      assert.equal(rows[1].ownRunPid, child.pid);
      assert.equal(rows[1].cleanupMethod, 'process-handle');
      assert.equal(rows[1].cleanupCompletenessUnverified, true);
    } finally {
      if (child.exitCode === null && child.signalCode === null) child.kill();
      assert.equal(path.dirname(root), os.tmpdir());
      assert.match(path.basename(root), /^soui-series-gate-/);
      fs.rmSync(root, {recursive: true, force: true});
    }
  });

test('live gate checks the real runner identity before stopping its process tree',
  {skip: process.platform !== 'win32'}, async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-series-gate-'));
    const gateFile = path.join(root, 'gates.ndjson');
    fs.writeFileSync(gateFile, '');
    const child = spawn(process.execPath, ['-e', 'setInterval(() => {}, 1000)'],
      {stdio: 'ignore', windowsHide: true});
    try {
      await assert.rejects(monitorChild(child, gateFile, 1, 'soui', (pid, observed) => ({
        ...liveInterferenceCheck(pid, observed),
        competingProcesses: [{pid: 98765, name: 'qemu-system-x86_64.exe'}],
      }), 5000), /External workload appeared/);
      const rows = fs.readFileSync(gateFile, 'utf8').trim().split('\n').map(JSON.parse);
      assert.equal(rows[1].cleanupMethod, 'verified-tree');
      assert.equal(rows[1].cleanupCompletenessUnverified, false);
      assert.equal(rows[1].ownRunPid, child.pid);
    } finally {
      if (child.exitCode === null && child.signalCode === null) child.kill();
      assert.equal(path.dirname(root), os.tmpdir());
      assert.match(path.basename(root), /^soui-series-gate-/);
      fs.rmSync(root, {recursive: true, force: true});
    }
  });

test('live gate fails closed and stops its runner when the process query fails',
  {skip: process.platform !== 'win32'}, async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-series-gate-'));
    const gateFile = path.join(root, 'gates.ndjson');
    fs.writeFileSync(gateFile, '');
    const child = spawn(process.execPath, ['-e', 'setInterval(() => {}, 1000)'],
      {stdio: 'ignore', windowsHide: true});
    try {
      await assert.rejects(monitorChild(child, gateFile, 2, 'electron', () => {
        throw new Error('synthetic scanner failure');
      }, 5000), /synthetic scanner failure/);
      const rows = fs.readFileSync(gateFile, 'utf8').trim().split('\n').map(JSON.parse);
      assert.match(rows[0].gateError, /synthetic scanner failure/);
      assert.equal(rows[1].cleanupReason, 'gate-error');
      assert.equal(rows[1].ownRunPid, child.pid);
      assert.equal(rows[1].cleanupMethod, 'process-handle');
    } finally {
      if (child.exitCode === null && child.signalCode === null) child.kill();
      assert.equal(path.dirname(root), os.tmpdir());
      assert.match(path.basename(root), /^soui-series-gate-/);
      fs.rmSync(root, {recursive: true, force: true});
    }
  });

test('live gate lets an isolated runner exit normally',
  {skip: process.platform !== 'win32'}, async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-series-gate-'));
    const gateFile = path.join(root, 'gates.ndjson');
    fs.writeFileSync(gateFile, '');
    const child = spawn(process.execPath, ['-e', 'setTimeout(() => {}, 150)'],
      {stdio: 'ignore', windowsHide: true});
    try {
      await monitorChild(child, gateFile, 3, 'soui', pid => ({
        checkedAt: new Date().toISOString(), owned: new Map([[pid, 'owned']]),
        rootPresent: true,
        competingProcesses: [],
      }), 5000);
      const rows = fs.readFileSync(gateFile, 'utf8').trim().split('\n').map(JSON.parse);
      assert.equal(rows.length, 1);
      assert.deepEqual(rows[0].competingProcesses, []);
      assert.equal(rows[0].cleanupReason, undefined);
    } finally {
      if (child.exitCode === null && child.signalCode === null) child.kill();
      assert.equal(path.dirname(root), os.tmpdir());
      assert.match(path.basename(root), /^soui-series-gate-/);
      fs.rmSync(root, {recursive: true, force: true});
    }
  });

test('live gate refuses a sample that cannot identify its own runner',
  {skip: process.platform !== 'win32'}, async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-series-gate-'));
    const gateFile = path.join(root, 'gates.ndjson');
    fs.writeFileSync(gateFile, '');
    const child = spawn(process.execPath, ['-e', 'setInterval(() => {}, 1000)'],
      {stdio: 'ignore', windowsHide: true});
    try {
      await assert.rejects(monitorChild(child, gateFile, 3, 'soui', pid => ({
        checkedAt: new Date().toISOString(), owned: new Map([[pid, 'owned']]),
        rootPresent: false, competingProcesses: [],
      }), 5000), /runner process identity was absent/);
      const rows = fs.readFileSync(gateFile, 'utf8').trim().split('\n').map(JSON.parse);
      assert.equal(rows[0].rootPresent, false);
      assert.equal(rows[1].cleanupReason, 'runner-identity-absent');
      assert.equal(rows[1].cleanupCompletenessUnverified, true);
    } finally {
      if (child.exitCode === null && child.signalCode === null) child.kill();
      assert.equal(path.dirname(root), os.tmpdir());
      assert.match(path.basename(root), /^soui-series-gate-/);
      fs.rmSync(root, {recursive: true, force: true});
    }
  });

test('live gate catches interference that starts after a clean initial sample',
  {skip: process.platform !== 'win32'}, async () => {
    const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-series-gate-'));
    const gateFile = path.join(root, 'gates.ndjson');
    fs.writeFileSync(gateFile, '');
    const child = spawn(process.execPath, ['-e', 'setInterval(() => {}, 1000)'],
      {stdio: 'ignore', windowsHide: true});
    let samples = 0;
    try {
      await assert.rejects(monitorChild(child, gateFile, 4, 'electron', pid => ({
        checkedAt: new Date().toISOString(), owned: new Map([[pid, 'owned']]),
        rootPresent: true,
        competingProcesses: ++samples === 1 ? [] :
          [{pid: 98765, name: 'qemu-system-x86_64.exe'}],
      }), 50), /External workload appeared/);
      const rows = fs.readFileSync(gateFile, 'utf8').trim().split('\n').map(JSON.parse);
      assert.equal(rows[0].competingProcesses.length, 0);
      assert.equal(rows[1].competingProcesses[0].pid, 98765);
      assert.equal(rows[2].cleanupReason, 'external-workload');
      assert.equal(samples, 2);
    } finally {
      if (child.exitCode === null && child.signalCode === null) child.kill();
      assert.equal(path.dirname(root), os.tmpdir());
      assert.match(path.basename(root), /^soui-series-gate-/);
      fs.rmSync(root, {recursive: true, force: true});
    }
  });
