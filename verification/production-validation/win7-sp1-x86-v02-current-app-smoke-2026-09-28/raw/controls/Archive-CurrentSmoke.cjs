'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const guest = path.join(root, 'guest-app-smoke-attempt1');
const target = 'C:/SOUI-SDK-development/preview14-scheduler/verification/production-validation/win7-sp1-x86-v02-current-app-smoke-2026-09-28';
assert.equal(fs.existsSync(target), false, 'Refuse to replace evidence');
const read = file => fs.readFileSync(file);
const json = file => JSON.parse(read(file).toString('utf8').replace(/^\uFEFF/, ''));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const freeze = read(path.join(root, 'app-smoke-attempt1/freeze.json'));
const input = json(path.join(root, 'app-smoke-attempt1/input.json'));
const review = json(path.join(guest, 'smoke-review.json'));
const qemu = json(path.join(guest, 'qemu-result.json'));
const gates = json(path.join(guest, 'gates-during.json'));
const serial = read(path.join(guest, 'serial-export.txt'));
assert.equal(sha(freeze), '6b11fc16ae74e00e528d14ab7aa301b49034348b8f8446a192c32b28c57445a3');
assert.equal(review.freezeSha256, sha(freeze));
assert.equal(review.serialSha256, sha(serial));
assert.equal(review.serialFrames, 29);
assert.equal(review.currentWorkbenchShortSmokeVerified, true);
assert.equal(review.v02RealInputVerified, false);
assert.equal(review.win7CurrentV02Verified, false);
assert.equal(qemu.exitCode, 0);
assert.equal(qemu.timedOut, false);
assert.equal(qemu.externalLoadObserved, false);
assert.equal(qemu.diskUnchanged, true);
assert.equal(qemu.frozenCheckExitCode, 0);
assert(gates.length > 0);
for (const gate of gates) { assert.equal(gate.error, null); assert.deepEqual(gate.processes, []); }
assert.equal(input.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(fs.readdirSync(guest).filter(name => /^monitor-run-.*\.log$/.test(name)).length, 1);
for (const name of ['boot-check-1', 'during-smoke-1', 'during-smoke-2'])
  assert.match(read(path.join(guest, `${name}.ppm`)).subarray(0, 32).toString('ascii'), /^P6\s+800\s+600\s+255\s/);

const pairs = [];
const add = (source, destination) => pairs.push([source, destination]);
function tree(source, destination) {
  for (const item of fs.readdirSync(source, {withFileTypes: true})) {
    const file = path.join(source, item.name);
    const relative = path.join(destination, item.name);
    if (item.isDirectory()) tree(file, relative);
    else if (item.isFile()) add(file, relative);
    else throw Error(`Unsupported evidence item: ${file}`);
  }
}
tree(guest, 'guest');
for (const file of ['Archive-CurrentSmoke.cjs', 'Prepare-CurrentSmoke.cjs',
  'Freeze-CurrentSmoke.cjs', 'Verify-CurrentSmoke.cjs', 'Prepare-SmokeDriver.cjs',
  'Launch-CurrentSmoke.ps1', 'Run-CurrentSmoke.ps1', 'Monitor-CurrentSmoke.cjs',
  'Inspect-SmokeSerial.cjs', 'Review-CurrentSmoke.cjs',
  'app-smoke-attempt1/freeze.json', 'app-smoke-attempt1/input.json',
  'app-smoke-attempt1/volume/soui-input/transfer-files.tsv',
  'app-smoke-attempt1/volume/soui-input/run-current-smoke.ps1',
  'app-smoke-attempt1/volume/soui-input/run-current-smoke.cmd',
  'app-smoke-attempt1/volume/soui-input/verify-apps.ps1',
  'app-smoke-attempt1/volume/soui-input/export-serial.ps1',
  'app-smoke-attempt1/volume/soui-input/SouiStarter-payload.tsv',
  'app-smoke-attempt1/volume/soui-input/SouiWorkbench-payload.tsv'])
  add(path.join(root, file), path.join('controls', file));
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
  schema: 'soui.win7-v02-current-app-smoke-raw/v1', fileCount: files.length, files,
  v02RealInputVerified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify({
  schema: 'soui.win7-v02-current-app-smoke-summary/v1',
  sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: sha(freeze), serialSha256: sha(serial),
  serialBytes: serial.length, serialFrames: review.serialFrames,
  guestIdentity: review.guestIdentity, qemuPid: qemu.pid,
  gateSamples: gates.length, externalLoadObserved: false,
  applicationNativeExitCode: 0, applicationReady: true, bridgeReady: true,
  fullClose: true, completePayloadsAndLegalPrePost: true,
  currentWorkbenchShortSmokeVerified: true, dataPageVisibleVerified: false,
  v02RealInputVerified: false, win7CurrentV02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, gateSamples: gates.length,
  serialFrames: review.serialFrames, currentWorkbenchShortSmokeVerified: true,
  win7CurrentV02Verified: false, releaseReady: false}));
