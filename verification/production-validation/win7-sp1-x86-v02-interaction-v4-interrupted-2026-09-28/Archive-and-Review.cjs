'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const source = 'C:/SOUI-SDK-production-validation/preview15-v02-win7-current-20260927';
const attempt = 'guest-layout-interaction-v4-attempt1';
const freezeSha256 = '4532ea7aa7bce0858d391d06339e8779dc9ad3aab7660f5c7e44a1b58077b4af';
const raw = path.join(root, 'raw');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const from = name => path.join(source, attempt, name);
const to = name => path.join(raw, name);
const json = name => JSON.parse(fs.readFileSync(to(name), 'utf8').replace(/^\uFEFF/, ''));
const mode = process.argv[2];

if (mode === 'archive') {
  assert.equal(fs.existsSync(raw), false, 'Refuse to replace archived evidence');
  const entries = fs.readdirSync(path.join(source, attempt), {withFileTypes: true});
  assert(entries.every(entry => entry.isFile()));
  const excluded = entries.filter(entry => entry.name.startsWith('vl.')).map(entry => entry.name).sort();
  assert.equal(excluded.length, 2);
  const files = entries.filter(entry => !entry.name.startsWith('vl.'))
    .map(entry => entry.name).sort();
  assert(files.includes('command-send.json'));
  assert(files.includes('desktop-check-1.png'));
  assert(files.includes('post-run-1.png'));
  assert(files.includes('serial-export.txt'));
  fs.mkdirSync(raw);
  const items = files.map(name => {
    const original = from(name);
    const target = to(name);
    fs.copyFileSync(original, target, fs.constants.COPYFILE_EXCL);
    const bytes = fs.readFileSync(original);
    assert.equal(sha(fs.readFileSync(target)), sha(bytes));
    return {name, bytes: bytes.length, sha256: sha(bytes), sourceMtimeUtc: fs.statSync(original).mtime.toISOString()};
  });
  const record = {schema: 'soui.win7-v02-v4-interrupted-evidence/v1', attempt,
    freezeSha256, excludedGuestTempSnapshots: excluded, items};
  fs.writeFileSync(to('copy-review.json'), `${JSON.stringify(record, null, 2)}\n`, {flag: 'wx'});
} else if (mode !== 'review' && mode !== '--check-index') {
  throw Error('Usage: node Archive-and-Review.cjs archive|review|--check-index');
}

const review = json('copy-review.json');
assert.equal(review.schema, 'soui.win7-v02-v4-interrupted-evidence/v1');
assert.equal(review.attempt, attempt);
assert.equal(review.freezeSha256, freezeSha256);
for (const item of review.items) {
  assert.match(item.name, /^[A-Za-z0-9][A-Za-z0-9._-]*$/);
  const bytes = fs.readFileSync(to(item.name));
  assert.equal(bytes.length, item.bytes, item.name);
  assert.equal(sha(bytes), item.sha256, item.name);
  assert.equal(sha(fs.readFileSync(from(item.name))), item.sha256, `original: ${item.name}`);
  if (mode === '--check-index') {
    const repo = path.resolve(root, '../../..');
    const relative = path.relative(repo, to(item.name)).split(path.sep).join('/');
    const staged = cp.execFileSync('git', ['-C', repo, 'show', `:${relative}`],
      {encoding: null, windowsHide: true, maxBuffer: item.bytes + 1024});
    assert.equal(staged.length, item.bytes, `index size: ${item.name}`);
    assert.equal(sha(staged), item.sha256, `index SHA-256: ${item.name}`);
  }
}

const launch = json('launch.json');
const command = json('command-send.json');
const qemu = json('qemu-result.json');
const gateValue = json('gates-during.json');
const gates = Array.isArray(gateValue) ? gateValue : [gateValue];
assert.equal(launch.freezeSha256, freezeSha256);
assert.equal(launch.pid, command.pid);
assert.equal(launch.pid, qemu.pid);
assert.equal(command.command, 'cmd.exe /c D:\\soui-input\\run-display-explore.cmd');
assert(new Date(launch.startedAt) <= new Date(command.sentAt));
assert(new Date(command.sentAt) <= new Date(qemu.finishedAt));
assert.equal(qemu.exitCode, -1);
assert.equal(qemu.externalLoadObserved, true);
assert.equal(qemu.timedOut, false);
assert.equal(qemu.diskUnchanged, true);
assert.equal(qemu.diskSha256Before, qemu.diskSha256After);
assert.equal(qemu.frozenCheckExitCode, 0);
const external = gates.flatMap(sample => sample.processes || [])
  .filter(process => process.ProcessId !== launch.pid && /^qemu-system/.test(process.Name));
assert(external.length > 0);
assert(gates.some(sample => (sample.processes || []).some(process =>
  process.ProcessId === 54876 &&
  new Date(sample.at) > new Date(command.sentAt) &&
  new Date(sample.at) <= new Date(qemu.finishedAt))));
assert(external.some(process => process.ProcessId === 54876 &&
  Number(/\/Date\(([0-9]+)\)\//.exec(process.CreationDate)?.[1]) >
    new Date(command.sentAt).getTime()));
assert.equal(fs.statSync(to('serial-export.txt')).size, 0);
assert.match(fs.readFileSync(to('desktop-check-1.ppm')).subarray(0, 32).toString('ascii'),
  /^P6\n800 600\n255\n/);
assert.match(fs.readFileSync(to('post-run-1.ppm')).subarray(0, 32).toString('ascii'),
  /^P6\n800 600\n255\n/);
assert.equal(fs.readFileSync(to('frozen-check-before.log')).equals(
  fs.readFileSync(to('frozen-check-after.log'))), true);
console.log(JSON.stringify({attempt, files: review.items.length,
  indexChecked: mode === '--check-index', ownQemuPid: launch.pid,
  externalQemuPids: [...new Set(external.map(process => process.ProcessId))],
  guestCommandSent: true, serialBytes: 0, applicationVerified: false,
  diskUnchanged: true, win7CurrentV02Verified: false, releaseReady: false}));
