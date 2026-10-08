'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = __dirname;
const repo = execFileSync('git', ['rev-parse', '--show-toplevel'], {
  cwd: root, encoding: 'utf8', windowsHide: true,
}).trim();
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const relative = name => path.relative(repo, path.join(root, name)).replaceAll('\\', '/');
const expected = new Map([
  ['red-attempt1', 1],
  ['green-targeted-attempt1', 0],
  ['green-full-attempt1', 0],
]);
for (const [label, exitCode] of expected) {
  const resultName = `${label}.result.json`;
  const result = JSON.parse(fs.readFileSync(path.join(root, resultName), 'utf8'));
  assert.equal(result.schema, 'soui.benchmark-pid-chronology-test/v1');
  assert.equal(result.label, label);
  assert.equal(result.exitCode, exitCode);
  assert.equal(result.error, null);
  assert.equal(result.hostIsolationClaim, false);
  assert.equal(result.benchmarkValid, false);
  assert.equal(result.releaseReady, false);
  for (const kind of ['stdout', 'stderr']) {
    const name = `${label}.${kind}.txt`;
    const bytes = fs.readFileSync(path.join(root, name));
    assert.equal(bytes.length, result[`${kind}Bytes`]);
    assert.equal(sha(bytes), result[`${kind}Sha256`]);
    const indexed = execFileSync('git', ['show', `:${relative(name)}`], {
      cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024,
    });
    assert.deepEqual(indexed, bytes, `${name} Git index bytes`);
  }
  const resultBytes = fs.readFileSync(path.join(root, resultName));
  const indexedResult = execFileSync('git', ['show', `:${relative(resultName)}`], {
    cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024,
  });
  assert.deepEqual(indexedResult, resultBytes, `${resultName} Git index bytes`);
}
const red = fs.readFileSync(path.join(root, 'red-attempt1.stdout.txt'), 'utf8');
const targeted = fs.readFileSync(path.join(root, 'green-targeted-attempt1.stdout.txt'), 'utf8');
const full = fs.readFileSync(path.join(root, 'green-full-attempt1.stdout.txt'), 'utf8');
assert.match(red, /live gate does not adopt an older process/);
assert.match(red, /actual: \[ 100, 200 \]/);
assert.match(targeted, /pass 1/);
assert.match(full, /pass 20/);
assert.match(full, /fail 0/);
const observedName = 'live-gate-observation-attempt1.json';
const observedBytes = fs.readFileSync(path.join(root, observedName));
const observed = JSON.parse(observedBytes);
assert.equal(observed.schema, 'soui.benchmark-live-gate-observation/v1');
assert.equal(observed.expectedExternalPid, 44672);
assert.equal(observed.detectedProcess.pid, observed.expectedExternalPid);
assert.equal(observed.detectedProcess.name.toLowerCase(), 'qemu-system-x86_64.exe');
assert.equal(observed.rootPresent, true);
assert.equal(observed.hostIsolationClaim, false);
assert.equal(observed.benchmarkValid, false);
assert.equal(observed.releaseReady, false);
assert.equal(sha(fs.readFileSync(path.join(repo, 'benchmarks/protocol/run-series.cjs'))),
  'd3edaefdf0ca2e4f337ec7446499a82b65774cfbd8cba45de507d8fde40c7af3');
assert.deepEqual(execFileSync('git', ['show', `:${relative(observedName)}`], {
  cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024,
}), observedBytes);
console.log(JSON.stringify({attempts: 3, rawFiles: 10,
  redExit: 1, targetedExit: 0, fullExit: 0, indexMatchesWorking: true}));
