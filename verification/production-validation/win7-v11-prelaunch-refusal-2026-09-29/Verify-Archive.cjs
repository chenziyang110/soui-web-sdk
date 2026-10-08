'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');

const root = __dirname;
const repo = path.resolve(root, '../../..');
const prefix = 'verification/production-validation/win7-v11-prelaunch-refusal-2026-09-29';
const compareOriginals = process.argv[2] === '--originals';
assert(process.argv.length === 2 || compareOriginals);
const originRoot = 'C:/SOUI-SDK-production-validation/work-area-placement-20260928';
const parseJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const entries = parseJson(path.join(root, 'raw-manifest.json'));
assert(Array.isArray(entries) && entries.length === 10);
const names = fs.readdirSync(path.join(root, 'raw')).sort();
assert.deepEqual(names, entries.map(entry => entry.name).sort());
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
for (const entry of entries) {
  assert.match(entry.name, /^[a-z0-9][a-z0-9.-]*$/);
  assert(Number.isSafeInteger(entry.bytes) && entry.bytes >= 0);
  assert.match(entry.sha256, /^[a-f0-9]{64}$/);
  const bytes = fs.readFileSync(path.join(root, 'raw', entry.name));
  assert.equal(bytes.length, entry.bytes, entry.name);
  assert.equal(sha(bytes), entry.sha256, entry.name);
  const staged = spawnSync('git', ['show', `:${prefix}/raw/${entry.name}`], {
    cwd: repo, encoding: null, maxBuffer: 1024 * 1024,
  });
  assert.equal(staged.status, 0, `${entry.name}: ${staged.stderr}`);
  assert(staged.stdout.equals(bytes), `${entry.name}: Git index bytes differ`);
  if (compareOriginals) {
    const original = entry.name.startsWith('driver-')
      ? path.join(originRoot, 'command-logs/guest-native-resize-v11-driver-attempt1',
        entry.name.slice('driver-'.length))
      : entry.name.startsWith('transfer-')
        ? path.join(originRoot, 'win7-v11-transfer-attempt1',
          entry.name.slice('transfer-'.length))
        : path.join(originRoot, 'guest-native-resize-v11-attempt1', entry.name);
    assert(fs.readFileSync(original).equals(bytes), `${entry.name}: original bytes differ`);
  }
}
const attempt = path.join(root, 'raw');
const gate = parseJson(path.join(attempt, 'gate-before-launch.json'));
assert.equal(gate.processes.length, 1);
assert.match(gate.processes[0].Name, /^qemu-system-/);
assert.equal(fs.readFileSync(path.join(attempt, 'driver-exit.txt'), 'utf8').trim(), '1');
assert.match(fs.readFileSync(path.join(attempt, 'driver-stderr.log'), 'utf8'),
  /Desktop load appeared before QEMU launch; no VM started/);
const freeze = parseJson(path.join(attempt, 'transfer-freeze.json'));
assert.equal(sha(fs.readFileSync(path.join(attempt, 'transfer-freeze.json'))),
  '6d712b263f6318aacd8077418d1906aa013104ed6c7113ca2faf98c637716aa2');
assert.equal(freeze.guestExecuted, false);
console.log(JSON.stringify({status: 'verified', rawFiles: entries.length,
  originalBytesMatched: compareOriginals, qemuStartedByAttempt: false,
  commandSent: false, releaseReady: false}));
