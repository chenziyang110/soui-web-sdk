'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const external = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927';
const target = 'C:/SOUI-SDK-development/preview14-scheduler/verification/production-validation/win7-sp1-x86-v02-current-preflight-2026-09-27';
assert.equal(fs.existsSync(target), false, 'Refuse to replace preflight archive');
const guest = path.join(root, 'guest-preflight-attempt1');
const read = file => fs.readFileSync(file);
const json = file => JSON.parse(read(file).toString('utf8').replace(/^\uFEFF/, ''));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const freeze = read(path.join(root, 'attempt1/freeze.json'));
const input = json(path.join(root, 'attempt1/preflight-input.json'));
const review = json(path.join(guest, 'preflight-review.json'));
const qemu = json(path.join(guest, 'qemu-result.json'));
const gates = json(path.join(guest, 'gates-during.json'));
const serial = read(path.join(guest, 'serial-export.txt'));
assert.equal(sha(freeze), 'b5137aa787b1885e2d11f7152292de15421ec8f0dfe1da5b72240041b794b4f0');
assert.equal(review.freezeSha256, sha(freeze));
assert.equal(review.serialSha256, sha(serial));
assert.equal(review.frameCount, 9);
assert.equal(review.guestPreflightVerified, true);
assert.equal(review.applicationLaunched, false);
assert.equal(review.win7CurrentV02Verified, false);
assert.equal(qemu.exitCode, 0);
assert.equal(qemu.timedOut, false);
assert.equal(qemu.externalLoadObserved, false);
assert.equal(qemu.diskUnchanged, true);
assert.equal(qemu.frozenCheckExitCode, 0);
assert(gates.length > 0);
for (const gate of gates) {
  assert.equal(gate.error, null);
  assert.deepEqual(gate.processes, []);
}
assert.equal(input.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(input.guestApplicationLaunch, false);
for (const name of ['boot-1', 'after-command-1', 'after-complete']) {
  assert.match(read(path.join(guest, `${name}.ppm`)).subarray(0, 32).toString('ascii'), /^P6\s+800\s+600\s+255\s/);
}
assert.equal(fs.readdirSync(guest).filter(name => /^monitor-run-.*\.log$/.test(name)).length, 1);
for (const name of ['win7-preflight-transport-attempt1', 'win7-preflight-freeze-attempt1',
  'win7-preflight-verify-attempt1']) {
  const result = json(path.join(external, 'command-logs', name, 'result.json'));
  assert.equal(result.exitCode, 0);
  assert.equal(result.interruptedByExternal, null);
  assert.equal(result.error, null);
}
const interrupted = json(path.join(external, 'command-logs/source-manifest-create-win7v02prep-attempt1/result.json'));
assert.equal(interrupted.exitCode, 1);
assert.equal(interrupted.cleanupReason, 'external-workload');
assert(interrupted.interruptedByExternal?.length > 0);

const pairs = [];
const add = (file, dest) => pairs.push([file, dest]);
function tree(source, dest) {
  for (const item of fs.readdirSync(source, {withFileTypes: true})) {
    const file = path.join(source, item.name);
    const relative = path.join(dest, item.name);
    if (item.isDirectory()) tree(file, relative);
    else if (item.isFile()) add(file, relative);
    else throw Error(`Unsupported evidence item: ${file}`);
  }
}
tree(guest, 'guest');
for (const file of ['Archive-Preflight.cjs', 'Prepare-PreflightTransport.cjs',
  'Freeze-Preflight.cjs', 'Verify-Preflight.cjs', 'Review-PreflightSerial.cjs',
  'Launch-Preflight.ps1', 'Run-Guest.ps1', 'Monitor-Preflight.cjs',
  'harness/run-preflight.ps1', 'harness/run-preflight.cmd',
  'attempt1/freeze.json', 'attempt1/preflight-input.json',
  'attempt1/volume/soui-input/transfer-files.tsv']) {
  add(path.join(root, file), path.join('controls', file));
}
for (const name of ['source-manifest-create-win7v02prep-attempt1',
  'win7-preflight-transport-attempt1', 'win7-preflight-freeze-attempt1',
  'win7-preflight-verify-attempt1']) {
  tree(path.join(external, 'command-logs', name), path.join('commands', name));
}
const files = [];
for (const [source, destination] of pairs) {
  const dest = path.join(target, 'raw', destination);
  fs.mkdirSync(path.dirname(dest), {recursive: true});
  fs.copyFileSync(source, dest, fs.constants.COPYFILE_EXCL);
  const original = read(source);
  assert.equal(sha(read(dest)), sha(original));
  files.push({source, path: path.join('raw', destination).replaceAll('\\', '/'),
    bytes: original.length, sha256: sha(original), sourceMtimeUtc: fs.statSync(source).mtime.toISOString()});
}
files.sort((a, b) => a.path.localeCompare(b.path));
fs.writeFileSync(path.join(target, 'raw-manifest.json'), JSON.stringify({
  schema: 'soui.win7-v02-current-preflight-raw/v1', fileCount: files.length, files,
  applicationLaunched: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify({
  schema: 'soui.win7-v02-current-preflight-summary/v1',
  sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: sha(freeze), serialSha256: sha(serial),
  serialBytes: serial.length, serialFrames: review.frameCount,
  guestIdentity: review.guestIdentity, guestDisplay: review.display,
  guestInputLayouts: review.inputLayouts, qemuPid: qemu.pid,
  gateSamples: gates.length, externalLoadObserved: false,
  guestPreflightVerified: true, staticProbeLoadsInGuest: true,
  applicationLaunched: false, chineseImeInputVerified: false,
  win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, gateSamples: gates.length,
  serialFrames: review.frameCount, guestPreflightVerified: true,
  win7CurrentV02Verified: false, releaseReady: false}));
