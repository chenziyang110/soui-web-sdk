'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const raw = path.join(root, 'raw');
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = relative => fs.readFileSync(path.join(raw, relative));
const json = relative => JSON.parse(read(relative).toString('utf8').replace(/^\uFEFF/, ''));
const copy = json('copy-review.json');
assert.equal(copy.schema, 'soui.raw-evidence-copy/v1');
assert.equal(copy.sourceAttempt, 'guest-layout-interaction-v3-attempt1');
assert.equal(copy.files, copy.items.length);
for (const item of copy.items) {
  assert.match(item.path, /^[a-zA-Z0-9._/-]+$/);
  assert(!item.path.includes('..'));
  const bytes = read(item.path);
  assert.equal(bytes.length, item.bytes, item.path);
  assert.equal(sha256(bytes), item.sha256, item.path);
}
if (process.argv[2] === '--check-index') {
  const repo = path.resolve(root, '../../..');
  const prefix = path.relative(repo, raw).split(path.sep).join('/');
  for (const item of copy.items) {
    const staged = cp.execFileSync('git', ['-C', repo, 'show', `:${prefix}/${item.path}`],
      {encoding: null, windowsHide: true, maxBuffer: item.bytes + 1024});
    assert.equal(staged.length, item.bytes, `Git index length: ${item.path}`);
    assert.equal(sha256(staged), item.sha256, `Git index SHA-256: ${item.path}`);
  }
} else {
  assert.equal(process.argv.length, 2, 'Usage: node Review-Interruption.cjs [--check-index]');
}

const launch = json('guest-attempt/launch.json');
const qemu = json('guest-attempt/qemu-result.json');
const gates = json('guest-attempt/gates-during.json');
const driver = json('command-logs/guest-layout-interaction-v3-attempt1-driver/result.json');
const input = json('frozen-delta/input.json');
const freeze = read('frozen-delta/freeze.json');
assert.equal(input.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(input.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(sha256(freeze), '4a8eb139db54d993821da4e95a63f9100e14c3d3d91fab7afbdc34a6dad5e8a3');
assert.equal(launch.freezeSha256, sha256(freeze));
assert.equal(launch.pid, qemu.pid);
assert.equal(qemu.exitCode, -1);
assert.equal(qemu.externalLoadObserved, true);
assert.equal(qemu.timedOut, false);
assert.equal(qemu.diskUnchanged, true);
assert.equal(qemu.frozenCheckExitCode, 0);
assert.equal(qemu.diskSha256Before, qemu.diskSha256After);
assert.equal(driver.exitCode, 1);
assert.equal(driver.timedOut, false);
assert(Array.isArray(gates) && gates.length >= 3);
const external = gates.flatMap(sample => sample.processes || [])
  .filter(process => process.ProcessId !== launch.pid && /^qemu-system/.test(process.Name));
assert(external.length > 0);
assert.equal(read('guest-attempt/serial-export.txt').length, 0);
assert.equal(fs.existsSync(path.join(raw, 'guest-attempt/command-send.json')), false);
assert.equal(fs.existsSync(path.join(raw, 'guest-attempt/interaction-review-v3.json')), false);
assert.equal(fs.existsSync(path.join(raw, 'guest-attempt/desktop-check-001.png')), false);

const review = {
  schema: 'soui.win7-v02-interruption-review/v1',
  sourceSha256: input.sourceSha256,
  buildId: input.buildId,
  freezeSha256: sha256(freeze),
  copiedFilesVerified: copy.items.length,
  indexFilesVerified: process.argv[2] === '--check-index' ? copy.items.length : null,
  qemuPid: launch.pid,
  qemuExitCode: qemu.exitCode,
  driverExitCode: driver.exitCode,
  externalQemuPids: [...new Set(external.map(process => process.ProcessId))],
  externalLoadObserved: true,
  guestCommandSent: false,
  serialBytes: 0,
  applicationExecuted: false,
  diskUnchanged: true,
  win7CurrentV02Verified: false,
  releaseReady: false,
};
console.log(JSON.stringify(review));
