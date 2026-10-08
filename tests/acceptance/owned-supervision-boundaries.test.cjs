'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const identity = require('../../tools/internal/delivery-identity.cjs');

const filename = path.resolve(__dirname, 'runner.cjs');
const source = fs.readFileSync(filename, 'utf8');
const config = {mode: 'reload', cycles: 1, warmup: 0, settleMs: 500, timeoutMs: 20000};
const options = {timeout: 1000};

// An independently specified complete native protocol transcript provides the
// positive control. It does not claim native execution, resource acceptance or
// a real Job closure; only the orchestration failure semantics are tested here.
function transcript(pid = 125) {
  const sample = (phase, cycle) => ({event: 'sample', phase, cycle, privateBytes: 20000000,
    workingSetBytes: 10000000, handles: 100, threads: 3, gdiObjects: 2, userObjects: 3});
  const pair = (action, window, cycle) => ['step.begin', 'step.complete'].map(event => ({event, action, window, cycle}));
  return [{event: 'run.begin', ...config, pid}, sample('process', 0), ...pair('mount', 1, 0),
    ...pair('runtime.reject-busy', 0, 0), sample('mounted', 0), ...pair('reload', 1, 1),
    sample('cycle', 1), ...pair('close', 1, 2), sample('closed', 2), ...pair('runtime.shutdown', 0, 2),
    sample('runtime-closed', 2), ...pair('runtime.reject-restart', 0, 2),
    ...pair('runtime.shutdown-idempotent', 0, 2), {event: 'run.complete'}]
    .map((record, index) => JSON.stringify({...record, elapsedMs: index * 100})).join('\n') + '\n';
}

function fixture({readyError, setupError, owned = {}, log = transcript()} = {}) {
  const files = new Map();
  const closed = [];
  const suite = path.resolve(__dirname, '../../out/acceptance/fixture-only');
  let descriptor = 0;
  let stopped = 0;
  const completed = {pid: 125, creationFileTime: '133999999999999999', exitCode: 0,
    signal: null, spawnError: null, passed: true, closureConfirmed: true,
    terminal: {reason: 'completed'}, ...owned};
  const io = {
    mkdirSync() {},
    openSync() { return ++descriptor; },
    closeSync(value) { closed.push(value); },
    writeFileSync(name, data, settings) {
      assert.equal(settings.flag, 'wx');
      assert.equal(files.has(name), false);
      files.set(name, Buffer.from(data));
    },
    readFileSync(name) {
      if (path.basename(name) === 'events.ndjson') return Buffer.from(log);
      throw new Error('Unexpected fixture read: ' + name);
    },
  };
  const module = {exports: {}};
  const imports = name => {
    if (name === 'node:fs') return io;
    if (name === '../../tools/internal/owned-process.cjs') return {launchOwned: () => ({
      pid: completed.pid,
      ready: readyError ? Promise.reject(readyError) : Promise.resolve({pid: completed.pid}),
      finished: Promise.resolve(completed),
      stop() { ++stopped; return false; },
    })};
    if (name === './monotonic-supervisor.cjs') return {createSupervisor() {
      if (setupError) throw setupError;
      return {observe() {}, deadlineReason: () => null, snapshot: () => ({})};
    }};
    if (name === './timeout-diagnostics.cjs') return {captureTimeout() {
      throw new Error('Timeout diagnostics must not run in this fast boundary fixture');
    }};
    if (name.startsWith('./')) return require(path.resolve(__dirname, name));
    if (name.startsWith('../../')) return require(path.resolve(__dirname, name));
    return require(name);
  };
  imports.main = {};
  const sandbox = vm.createContext({module, exports: module.exports, require: imports,
    __dirname, __filename: filename, process: {env: {}}, Buffer, console,
    setTimeout: () => 1, clearTimeout() {}, setInterval: () => 2, clearInterval() {},
    fixtureDirectory: value => value,
    fixtureVerify: () => ({consumerBuildId: 'fixture', sourceSha256: 'fixture', payload: {sha256: 'fixture'}}),
  });
  // Keep the entire actual run() and transcript validators. Only expensive
  // build/directory preconditions are replaced, and run is exposed for this VM.
  vm.runInContext(source + '\ndirectory = fixtureDirectory; verify = fixtureVerify; module.exports.fixtureRun = run;\n',
    sandbox, {filename});
  return {
    async run() {
      const result = await module.exports.fixtureRun(suite, config);
      const resultFile = [...files.keys()].find(name => path.basename(name) === 'result.json');
      assert.ok(resultFile, 'Every supervision outcome must retain a result');
      assert.deepEqual(closed.sort(), [1, 2], 'Owned stdout/stderr descriptors must both close');
      return {result, saved: JSON.parse(files.get(resultFile).toString('utf8'))};
    },
    get stopped() { return stopped; },
  };
}

test('a completed subject with the full independent transcript remains a valid positive control', options, async () => {
  const {result, saved} = await fixture().run();
  assert.equal(result.passed, true);
  assert.equal(saved.passed, true);
  assert.equal(saved.outcome.pid, 125);
  assert.equal(saved.validation.completedSteps, 7);
  assert.equal(saved.validation.resourceGate, 'not-evaluated');
  assert.equal(saved.releaseReady, false);
  assert.equal(saved.logSha256, identity.hash(Buffer.from(transcript())));
});

test('supervisor setup failure cannot be overwritten by a completed Job and green native transcript', options, async () => {
  const state = fixture({setupError: new Error('Injected supervisor preparation failure')});
  const {result, saved} = await state.run();
  assert.equal(result.outcome.ownedProcess.passed, true, 'The green competing outcome must remain present');
  assert.equal(result.passed, false);
  assert.equal(saved.passed, false);
  assert.match(saved.outcome.spawnError, /supervisor preparation failure/);
  assert.ok(saved.failure);
  assert.equal(state.stopped, 1);
});

test('ready rejection after native completion remains a failed acceptance result', options, async () => {
  const state = fixture({readyError: new Error('Injected ready observation failure')});
  const {result, saved} = await state.run();
  assert.equal(result.outcome.ownedProcess.passed, true);
  assert.equal(result.passed, false);
  assert.equal(saved.passed, false);
  assert.match(saved.outcome.spawnError, /ready observation failure/);
  assert.ok(saved.failure);
});

test('a green native transcript cannot hide an unverified owned closure', options, async () => {
  const {result, saved} = await fixture({owned: {passed: false, closureConfirmed: false}}).run();
  assert.equal(result.passed, false);
  assert.equal(saved.passed, false);
  assert.ok(saved.failure);
});

test('acceptance transcript identity must match the owned subject identity', options, async () => {
  const {result, saved} = await fixture({owned: {pid: 126}}).run();
  assert.equal(result.passed, false);
  assert.equal(saved.passed, false);
  assert.match(saved.failure, /process identity/i);
});

test('a complete-looking native transcript without its terminal newline stays failed', options, async () => {
  const {result, saved} = await fixture({log: transcript().slice(0, -1)}).run();
  assert.equal(result.passed, false);
  assert.equal(saved.passed, false);
  assert.match(saved.failure, /truncated/i);
});
