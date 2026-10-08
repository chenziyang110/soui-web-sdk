'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const crypto = require('node:crypto');
const {EventEmitter} = require('node:events');

const filename = path.resolve(__dirname, '../../tools/internal/owned-process.cjs');
const source = fs.readFileSync(filename, 'utf8');
const first = {pid: 125, creationFileTime: '133999999999999999'};
const options = {timeout: 1000};

function start(identity = first) {
  return {schema: 'soui.owned-process-run/v1', event: 'start', ...identity,
    jobAssignedBeforeResume: true, breakawayAllowed: false};
}
function transcript(identity = first) {
  return [start(identity), {event: 'terminal', reason: 'completed', rootExited: true,
    rootExitCode: 0, passed: true, closureConfirmed: true, activeProcesses: 0,
    totalProcesses: 2}].map(row => JSON.stringify(row)).join('\n') + '\n';
}

// Exercise the actual client with deterministic I/O boundaries. These reports
// are fixtures, not evidence that a real Windows Job closed or Win7 passed.
function fixture() {
  const runner = 'C:\\owned-review\\runner.exe';
  const executable = 'C:\\owned-review\\subject.exe';
  const report = 'C:\\owned-review\\owned.ndjson';
  const files = new Map([[runner, Buffer.from('runner bytes')], [executable, Buffer.from('subject bytes')]]);
  const child = new EventEmitter();
  child.pid = 999;
  child.exitCode = null;
  child.signalCode = null;
  child.stdin = new EventEmitter();
  child.stdin.destroyed = false;
  child.stdin.writable = true;
  child.stdin.end = () => {};
  child.kill = () => {};
  let poll;
  let readError = null;
  let hashError = null;
  let reportReads = 0;
  const io = {
    existsSync: name => files.has(name),
    writeFileSync(name, data, config) {
      assert.equal(config.flag, 'wx');
      assert.equal(files.has(name), false);
      files.set(name, Buffer.from(data));
    },
    readFileSync(name, encoding) {
      if (name === report) {
        ++reportReads;
        if (readError) throw readError;
      }
      if (!files.has(name)) throw new Error('Missing fixture file: ' + name);
      const bytes = Buffer.from(files.get(name));
      return encoding ? bytes.toString(encoding) : bytes;
    },
  };
  const hashing = {
    createHash(algorithm) {
      const actual = crypto.createHash(algorithm);
      let input;
      const wrapper = {
        update(bytes) { input = bytes; actual.update(bytes); return wrapper; },
        digest(encoding) {
          if (hashError && Buffer.isBuffer(input) && input.equals(files.get(report))) throw hashError;
          return actual.digest(encoding);
        },
      };
      return wrapper;
    },
  };
  const module = {exports: {}};
  const sandbox = {
    module, process: {platform: 'win32', env: {}},
    require(name) {
      if (name === 'node:fs') return io;
      if (name === 'node:path') return path.win32;
      if (name === 'node:crypto') return hashing;
      if (name === 'node:child_process') return {spawn: () => child};
      throw new Error('Unexpected fixture import: ' + name);
    },
    setInterval(callback) { poll = callback; return 1; },
    clearInterval() {}, setTimeout: () => 2, clearTimeout() {},
  };
  vm.runInNewContext(source, sandbox, {filename});
  const run = module.exports.launchOwned(executable, [], {
    runner, cwd: 'C:\\owned-review', report, timeoutMs: 1000,
  });
  return {
    run, child, report,
    setReport: text => files.set(report, Buffer.from(text)),
    observe: () => poll(),
    failRead: error => { readError = error; },
    failHash: error => { hashError = error; },
    get reportReads() { return reportReads; },
    finish() { child.exitCode = 0; child.emit('close', 0, null); },
  };
}

test('a complete fast close before the first poll publishes the subject and finishes normally', options, async () => {
  const state = fixture();
  const text = transcript();
  state.setReport(text);
  state.finish();
  const ready = await state.run.ready;
  const result = await state.run.finished;
  assert.equal(ready.pid, first.pid);
  assert.notEqual(ready.pid, state.child.pid);
  assert.equal(result.pid, ready.pid);
  assert.equal(result.creationFileTime, ready.creationFileTime);
  assert.equal(result.passed, true);
  assert.equal(result.reportSha256, crypto.createHash('sha256').update(text).digest('hex'));
  assert.equal(state.reportReads, 1, 'Final parsing and hashing must consume one report snapshot');
});

test('final evidence cannot replace either part of the already published subject identity', options, async t => {
  for (const changed of [{...first, pid: first.pid + 1}, {...first, creationFileTime: '134000000000000000'}]) {
    await t.test(JSON.stringify(changed), async () => {
      const state = fixture();
      state.setReport(JSON.stringify(start()) + '\n');
      state.observe();
      const ready = await state.run.ready;
      state.setReport(transcript(changed));
      state.finish();
      const result = await state.run.finished;
      assert.equal(result.passed, false);
      assert.match(result.reportError, /identity|published/i);
      assert.equal(result.pid, ready.pid);
      assert.equal(result.creationFileTime, ready.creationFileTime);
      assert.equal(state.run.pid, first.pid);
    });
  }
});

test('a final report read failure resolves finished as failure rather than escaping the close callback', options, async () => {
  const state = fixture();
  state.setReport(JSON.stringify(start()) + '\n');
  state.observe();
  await state.run.ready;
  state.setReport(transcript());
  state.failRead(new Error('EIO: final report read'));
  assert.doesNotThrow(() => state.finish());
  const result = await state.run.finished;
  assert.equal(result.passed, false);
  assert.equal(result.closureConfirmed, false);
  assert.equal(result.reportSha256, null);
  assert.match(result.reportError, /EIO: final report read/);
});

test('a report digest failure also resolves finished as failure', options, async () => {
  const state = fixture();
  state.setReport(transcript());
  state.failHash(new Error('Injected report digest failure'));
  assert.doesNotThrow(() => state.finish());
  await assert.rejects(state.run.ready, /report digest failure/);
  const result = await state.run.finished;
  assert.equal(result.passed, false);
  assert.equal(result.closureConfirmed, false);
  assert.equal(result.reportSha256, null);
  assert.match(result.reportError, /report digest failure/);
});

test('a rejected ready observation stays failed even if a complete green report appears before close', options, async () => {
  const state = fixture();
  state.setReport(JSON.stringify({...start(), pid: 0}) + '\n');
  // Model exit arriving before close: stop() cannot request termination after
  // the supervisor has exited, so the observation error itself must be sticky.
  state.child.exitCode = 0;
  state.observe();
  await assert.rejects(state.run.ready, /start identity/);
  state.setReport(transcript());
  state.finish();
  const result = await state.run.finished;
  assert.equal(result.stopRequested, false);
  assert.equal(result.terminal.passed, true);
  assert.equal(result.passed, false);
  assert.match(result.reportError, /start identity/);
});

test('a terminal without its final newline cannot pass despite a valid complete start', options, async () => {
  const state = fixture();
  state.setReport(transcript().slice(0, -1));
  state.finish();
  assert.equal((await state.run.ready).pid, first.pid);
  const result = await state.run.finished;
  assert.equal(result.passed, false);
  assert.equal(result.closureConfirmed, false);
  assert.match(result.reportError, /terminal|truncated/i);
});
