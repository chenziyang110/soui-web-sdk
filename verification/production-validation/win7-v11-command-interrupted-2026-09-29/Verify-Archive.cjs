'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');

const root = __dirname;
const repo = path.resolve(root, '../../..');
const prefix = 'verification/production-validation/win7-v11-command-interrupted-2026-09-29';
const originRoot = 'C:/SOUI-SDK-production-validation/work-area-placement-20260928';
const compareOriginals = process.argv[2] === '--originals';
assert(process.argv.length === 2 || (process.argv.length === 3 && compareOriginals));
const parse = name => JSON.parse(fs.readFileSync(path.join(root, 'raw', name), 'utf8').replace(/^\uFEFF/, ''));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'raw-manifest.json'), 'utf8'));
assert.equal(manifest.length, 25);
assert.deepEqual(fs.readdirSync(path.join(root, 'raw')).sort(), manifest.map(item => item.name).sort());
for (const item of manifest) {
  assert.match(item.name, /^[a-z0-9][a-z0-9.-]*$/);
  assert(Number.isSafeInteger(item.bytes) && item.bytes >= 0);
  assert.match(item.sha256, /^[a-f0-9]{64}$/);
  const bytes = fs.readFileSync(path.join(root, 'raw', item.name));
  assert.equal(bytes.length, item.bytes, item.name);
  assert.equal(sha(bytes), item.sha256, item.name);
  const staged = spawnSync('git', ['show', `:${prefix}/raw/${item.name}`],
    {cwd: repo, encoding: null, maxBuffer: 5 * 1024 * 1024});
  assert.equal(staged.status, 0, `${item.name}: Git index read failed`);
  assert(staged.stdout.equals(bytes), `${item.name}: Git index differs`);
  if (compareOriginals) {
    const original = item.name.startsWith('driver-')
      ? path.join(originRoot, 'command-logs/guest-native-resize-v11-driver-attempt2',
        item.name.slice('driver-'.length))
      : item.name.startsWith('transfer-')
        ? path.join(originRoot, 'win7-v11-transfer-attempt1',
          item.name.slice('transfer-'.length))
        : path.join(originRoot, 'guest-native-resize-v11-attempt2', item.name);
    assert(fs.readFileSync(original).equals(bytes), `${item.name}: original differs`);
  }
}
const freezeBytes = fs.readFileSync(path.join(root, 'raw', 'transfer-freeze.json'));
assert.equal(sha(freezeBytes), '6d712b263f6318aacd8077418d1906aa013104ed6c7113ca2faf98c637716aa2');
const gate = parse('gate.json');
const before = parse('gate-before-launch.json');
assert.equal(gate.processes.length, 0);
assert.equal(before.processes.length, 0);
const launch = parse('launch.json');
assert.equal(launch.pid, 64048);
assert.equal(launch.freezeSha256, sha(freezeBytes));
assert(launch.arguments.includes('-snapshot'));
assert(launch.arguments.includes('none'));
const send = parse('command-send.json');
assert.equal(send.pid, launch.pid);
assert.equal(send.command, 'cmd.exe /c D:\\soui-input\\run-display-explore.cmd');
const samples = parse('gates-during.json');
assert(samples.length > 20);
const external = samples.filter(sample => sample.processes.some(process => process.ProcessId === 21428));
assert(external.length >= 1);
assert(external.every(sample => new Date(sample.at) >= new Date(send.sentAt)));
const result = parse('qemu-result.json');
assert.equal(result.pid, launch.pid);
assert.equal(result.externalLoadObserved, true);
assert.equal(result.exitCode, -1);
assert.equal(result.frozenCheckExitCode, 0);
assert.equal(result.diskUnchanged, true);
assert.equal(result.diskSha256Before, result.diskSha256After);
assert.equal(result.releaseReady, false);
assert.equal(fs.statSync(path.join(root, 'raw', 'serial-export.txt')).size, 0);
assert.equal(fs.readFileSync(path.join(root, 'raw', 'driver-exit.txt'), 'utf8').trim(), '1');
assert(fs.statSync(path.join(root, 'raw', 'desktop-precommand-1.ppm')).size > 100000);
assert(fs.statSync(path.join(root, 'raw', 'desktop-aftercommand-1.ppm')).size > 100000);
console.log(JSON.stringify({status: 'verified', rawFiles: manifest.length,
  originalBytesMatched: compareOriginals, commandSent: true, serialBytes: 0,
  externalLoadObserved: true, compatibilityResult: 'unresolved', releaseReady: false}));
