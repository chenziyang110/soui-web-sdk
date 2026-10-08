'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');

const [label, pattern] = process.argv.slice(2);
assert.match(label || '', /^[a-z0-9-]+$/);
assert(pattern && pattern.length < 160);
const root = __dirname;
const cwd = 'C:\\SOUI-SDK-development\\benchmark-realtime-gate';
const prefix = path.join(root, label);
for (const suffix of ['.stdout.txt', '.stderr.txt', '.result.json'])
  assert.equal(fs.existsSync(prefix + suffix), false, `Refuse overwrite ${label}`);
const args = ['--test', `--test-name-pattern=${pattern}`,
  'tests/benchmark/series-interference.test.cjs'];
const startedAt = new Date().toISOString();
const run = spawnSync(process.execPath, args, {
  cwd, encoding: null, timeout: 120000, windowsHide: true,
  maxBuffer: 2 * 1024 * 1024,
});
const finishedAt = new Date().toISOString();
fs.writeFileSync(prefix + '.stdout.txt', run.stdout || Buffer.alloc(0), {flag: 'wx'});
fs.writeFileSync(prefix + '.stderr.txt', run.stderr || Buffer.alloc(0), {flag: 'wx'});
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const result = {
  schema: 'soui.benchmark-pid-chronology-test/v1', label, args,
  startedAt, finishedAt, exitCode: run.status, signal: run.signal,
  error: run.error ? String(run.error) : null,
  stdoutBytes: run.stdout?.length || 0,
  stdoutSha256: hash(run.stdout || Buffer.alloc(0)),
  stderrBytes: run.stderr?.length || 0,
  stderrSha256: hash(run.stderr || Buffer.alloc(0)),
  hostIsolationClaim: false, benchmarkValid: false, releaseReady: false,
};
fs.writeFileSync(prefix + '.result.json', JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({label, exitCode: run.status, error: result.error,
  stdoutBytes: result.stdoutBytes, stderrBytes: result.stderrBytes}));
