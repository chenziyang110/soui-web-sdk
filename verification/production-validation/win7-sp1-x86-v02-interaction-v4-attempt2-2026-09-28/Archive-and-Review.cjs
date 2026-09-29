'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const source = 'C:/SOUI-SDK-production-validation/preview15-v02-win7-current-20260927';
const attempt = 'guest-layout-interaction-v4-attempt2';
const raw = path.join(root, 'raw');
const freezeSha256 = '4532ea7aa7bce0858d391d06339e8779dc9ad3aab7660f5c7e44a1b58077b4af';
const serialSha256 = 'd0aab9b9c0384ababb610dab124d8ad7a912d1a9beca6d481bed331d823356ca';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const from = name => path.join(source, attempt, name);
const to = name => path.join(raw, name);
const json = name => JSON.parse(fs.readFileSync(to(name), 'utf8').replace(/^\uFEFF/, ''));
const mode = process.argv[2];

if (mode === 'archive') {
  assert.equal(fs.existsSync(raw), false, 'Refuse to replace archived evidence');
  const entries = fs.readdirSync(path.join(source, attempt), {withFileTypes: true});
  assert(entries.every(entry => entry.isFile()));
  const excluded = entries.filter(entry => entry.name.startsWith('vl.'))
    .map(entry => entry.name).sort();
  assert.equal(excluded.length, 0);
  const names = entries.filter(entry => !entry.name.startsWith('vl.'))
    .map(entry => entry.name).sort();
  for (const required of ['command-send.json', 'interaction-review-v4.json',
    'desktop-check-1.ppm', 'desktop-check-2.ppm', 'post-run-1.ppm',
    'post-run-external-processes.json', 'qemu-result.json', 'serial-export.txt'])
    assert(names.includes(required), `Missing ${required}`);
  fs.mkdirSync(raw);
  const items = names.map(name => {
    const bytes = fs.readFileSync(from(name));
    fs.copyFileSync(from(name), to(name), fs.constants.COPYFILE_EXCL);
    assert.equal(sha(fs.readFileSync(to(name))), sha(bytes));
    return {name, bytes: bytes.length, sha256: sha(bytes),
      sourceMtimeUtc: fs.statSync(from(name)).mtime.toISOString()};
  });
  const record = {schema: 'soui.win7-v02-v4-guest-evidence/v1', attempt,
    freezeSha256, excludedGuestTempSnapshots: excluded, items};
  fs.writeFileSync(to('copy-review.json'), `${JSON.stringify(record, null, 2)}\n`, {flag: 'wx'});
} else if (mode !== 'review' && mode !== '--check-index') {
  throw Error('Usage: node Archive-and-Review.cjs archive|review|--check-index');
}

const copied = json('copy-review.json');
assert.equal(copied.schema, 'soui.win7-v02-v4-guest-evidence/v1');
assert.equal(copied.attempt, attempt);
assert.equal(copied.freezeSha256, freezeSha256);
for (const item of copied.items) {
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
const review = json('interaction-review-v4.json');
const observation = json('post-run-external-processes.json');
const gateValue = json('gates-during.json');
const gates = Array.isArray(gateValue) ? gateValue : [gateValue];
assert.equal(launch.freezeSha256, freezeSha256);
assert.equal(launch.pid, 5708);
assert.equal(command.pid, launch.pid);
assert.equal(qemu.pid, launch.pid);
assert.equal(qemu.exitCode, 0);
assert.equal(qemu.timedOut, false);
assert.equal(qemu.externalLoadObserved, false);
assert.equal(qemu.gateFailure, null);
assert.equal(qemu.diskUnchanged, true);
assert.equal(qemu.frozenCheckExitCode, 0);
assert.equal(review.freezeSha256, freezeSha256);
assert.equal(review.serialSha256, serialSha256);
assert.equal(sha(fs.readFileSync(to('serial-export.txt'))), serialSha256);
assert.equal(review.serialBytes, 215389);
assert.equal(review.serialFrames, 96);
assert.equal(review.nativeInputActionsSent, 29);
assert.equal(review.applicationNativeExitCode, 0);
assert.equal(review.naturalClose, true);
assert.equal(review.resizeBoundsChecked, true);
for (const key of ['taskStartedVerified', 'imeCompositionVerified',
  'taskParallelVerified', 'visualScreenshotReviewed',
  'win7CurrentV02Verified', 'releaseReady']) assert.equal(review[key], false, key);
assert.equal(gates.length, 62);
assert(gates.every(gate => !gate.error && (!gate.processes || gate.processes.length === 0)));
assert.equal(observation.processes.length, 1);
const external = observation.processes[0];
assert.equal(external.ProcessId, 60968);
assert.equal(external.Name, 'qemu-system-x86_64.exe');
const externalStarted = new Date(external.CreationDate).getTime();
const lastGate = new Date(gates[gates.length - 1].at).getTime();
const finished = new Date(qemu.finishedAt).getTime();
const serialMtime = new Date(copied.items.find(item =>
  item.name === 'serial-export.txt').sourceMtimeUtc).getTime();
assert(Number.isFinite(externalStarted));
assert(serialMtime < lastGate && lastGate < externalStarted && externalStarted < finished);
assert(new Date(observation.observedAt).getTime() > finished);
console.log(JSON.stringify({attempt, files: copied.items.length,
  indexChecked: mode === '--check-index', ownQemuPid: launch.pid,
  externalQemuPid: external.ProcessId, lastGateToExternalMs: externalStarted - lastGate,
  serialBeforeExternal: true, fullHostIsolation: false,
  serialFrames: review.serialFrames, nativeInputActionsSent: review.nativeInputActionsSent,
  applicationNativeExitCode: review.applicationNativeExitCode,
  naturalClose: review.naturalClose, resizeBoundsChecked: review.resizeBoundsChecked,
  imeCompositionVerified: false, taskParallelVerified: false,
  win7CurrentV02Verified: false, releaseReady: false}));
