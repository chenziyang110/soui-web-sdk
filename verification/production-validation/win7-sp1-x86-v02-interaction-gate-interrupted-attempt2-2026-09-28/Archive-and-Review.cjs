'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const raw = path.join(root, 'raw');
const source = 'C:/SOUI-SDK-production-validation/preview15-v02-win7-current-20260927';
const commandSource = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
const attempt = 'guest-layout-interaction-v3-attempt2';
const driver = `${attempt}-driver`;
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const sourcePath = relative => relative.startsWith('guest-attempt/')
  ? path.join(source, attempt, relative.slice('guest-attempt/'.length))
  : path.join(commandSource, driver, relative.slice('driver/'.length));
const targetPath = relative => path.join(raw, relative);

if (process.argv[2] === 'archive') {
  assert.equal(fs.existsSync(raw), false, 'Refuse to replace archived evidence');
  const guest = fs.readdirSync(path.join(source, attempt), {withFileTypes: true})
    .filter(item => item.isFile() && !item.name.startsWith('vl.'))
    .map(item => `guest-attempt/${item.name}`);
  const files = [...guest, ...['result.json', 'stdout.log', 'stderr.log'].map(name => `driver/${name}`)]
    .sort();
  assert(files.includes('guest-attempt/desktop-check-1.ppm'));
  assert(files.includes('guest-attempt/desktop-check-1.png'));
  assert(!files.includes('guest-attempt/command-send.json'));
  const items = [];
  for (const relative of files) {
    const original = sourcePath(relative);
    const target = targetPath(relative);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.copyFileSync(original, target, fs.constants.COPYFILE_EXCL);
    const bytes = fs.readFileSync(original);
    assert.equal(hash(fs.readFileSync(target)), hash(bytes));
    items.push({path: relative, bytes: bytes.length, sha256: hash(bytes),
      source: original, sourceMtimeUtc: fs.statSync(original).mtime.toISOString()});
  }
  const record = {schema: 'soui.win7-v02-interrupted-evidence/v1', attempt,
    freezeSha256: '4a8eb139db54d993821da4e95a63f9100e14c3d3d91fab7afbdc34a6dad5e8a3',
    files: items.length, excludedGuestTempSnapshots: ['vl.N4AEW3', 'vl.XI3DW3'], items};
  fs.writeFileSync(path.join(raw, 'copy-review.json'), `${JSON.stringify(record, null, 2)}\n`, {flag: 'wx'});
} else if (process.argv[2] !== 'review' && process.argv[2] !== '--check-index') {
  throw Error('Usage: node Archive-and-Review.cjs archive|review|--check-index');
}

const copied = readJson(path.join(raw, 'copy-review.json'));
assert.equal(copied.schema, 'soui.win7-v02-interrupted-evidence/v1');
assert.equal(copied.attempt, attempt);
assert.equal(copied.files, copied.items.length);
for (const item of copied.items) {
  assert.match(item.path, /^(?:guest-attempt|driver)\/[a-zA-Z0-9._-]+$/);
  const bytes = fs.readFileSync(targetPath(item.path));
  assert.equal(bytes.length, item.bytes, item.path);
  assert.equal(hash(bytes), item.sha256, item.path);
  assert.equal(hash(fs.readFileSync(sourcePath(item.path))), item.sha256, `original: ${item.path}`);
}

if (process.argv[2] === '--check-index') {
  const repo = path.resolve(root, '../../..');
  const prefix = path.relative(repo, raw).split(path.sep).join('/');
  for (const item of copied.items) {
    const staged = cp.execFileSync('git', ['-C', repo, 'show', `:${prefix}/${item.path}`],
      {encoding: null, windowsHide: true, maxBuffer: item.bytes + 1024});
    assert.equal(staged.length, item.bytes, `index size: ${item.path}`);
    assert.equal(hash(staged), item.sha256, `index SHA-256: ${item.path}`);
  }
}

const qemu = readJson(targetPath('guest-attempt/qemu-result.json'));
const launch = readJson(targetPath('guest-attempt/launch.json'));
const gates = readJson(targetPath('guest-attempt/gates-during.json'));
const run = readJson(targetPath('driver/result.json'));
assert.equal(launch.freezeSha256, copied.freezeSha256);
assert.equal(launch.pid, qemu.pid);
assert.equal(qemu.exitCode, -1);
assert.equal(qemu.externalLoadObserved, true);
assert.equal(qemu.timedOut, false);
assert.equal(qemu.diskUnchanged, true);
assert.equal(qemu.diskSha256Before, qemu.diskSha256After);
assert.equal(qemu.frozenCheckExitCode, 0);
assert.equal(run.exitCode, 1);
assert.equal(run.timedOut, false);
const external = gates.flatMap(sample => sample.processes || [])
  .filter(process => process.ProcessId !== launch.pid && /^qemu-system/.test(process.Name));
assert(external.length > 0);
assert.equal(fs.statSync(targetPath('guest-attempt/serial-export.txt')).size, 0);
assert.equal(fs.existsSync(targetPath('guest-attempt/command-send.json')), false);
assert.equal(fs.existsSync(targetPath('guest-attempt/interaction-review-v3.json')), false);
const screenshot = fs.readFileSync(targetPath('guest-attempt/desktop-check-1.ppm'));
assert.match(screenshot.subarray(0, 32).toString('ascii'), /^P6\n800 600\n255\n/);
console.log(JSON.stringify({attempt, files: copied.items.length, indexChecked: process.argv[2] === '--check-index',
  ownQemuPid: launch.pid, externalQemuPids: [...new Set(external.map(process => process.ProcessId))],
  guestCommandSent: false, serialBytes: 0, applicationExecuted: false,
  diskUnchanged: true, win7CurrentV02Verified: false, releaseReady: false}));
