'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {spawnSync} = require('node:child_process');

const [flag, originArg] = process.argv.slice(2);
assert(process.argv.length === 2 || (process.argv.length === 4 && flag === '--originals' && originArg),
  'Usage: node Verify-Archive.cjs [--originals EXTERNAL_ROOT]');
const originRoot = originArg && path.resolve(originArg);
const root = __dirname;
const repo = path.resolve(root, '../../..');
const prefix = 'verification/production-validation/win7-v11-native-resize-2026-09-29';
const raw = path.join(root, 'raw');
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const parse = name => JSON.parse(fs.readFileSync(path.join(raw, name), 'utf8').replace(/^\uFEFF/, ''));
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'raw-manifest.json'), 'utf8'));
assert.equal(manifest.length, 43);
assert.deepEqual(fs.readdirSync(raw).sort(), manifest.map(item => item.name).sort());

for (const item of manifest) {
  assert.match(item.name, /^[a-z0-9][a-z0-9.-]*$/);
  assert.match(item.origin, /^(?:guest-native-resize-v11-attempt3|win7-v11-transfer-attempt1|command-logs\/guest-native-resize-v11-(?:driver|review)-attempt3|Review-V11\.cjs)/);
  assert(!item.origin.split('/').includes('..'));
  assert(Number.isSafeInteger(item.bytes) && item.bytes >= 0);
  assert.match(item.sha256, /^[a-f0-9]{64}$/);
  const bytes = fs.readFileSync(path.join(raw, item.name));
  assert.equal(bytes.length, item.bytes, item.name);
  assert.equal(sha256(bytes), item.sha256, item.name);
  const staged = spawnSync('git', ['show', `:${prefix}/raw/${item.name}`],
    {cwd: repo, encoding: null, maxBuffer: 5 * 1024 * 1024});
  assert.equal(staged.status, 0, `${item.name}: Git index read failed`);
  assert(staged.stdout.equals(bytes), `${item.name}: Git index differs`);
  if (originRoot) assert(fs.readFileSync(path.join(originRoot, item.origin)).equals(bytes),
    `${item.name}: original differs`);
}

const expectedFreeze = '6d712b263f6318aacd8077418d1906aa013104ed6c7113ca2faf98c637716aa2';
assert.equal(sha256(fs.readFileSync(path.join(raw, 'transfer-freeze.json'))), expectedFreeze);
const input = parse('transfer-input.json');
const freeze = parse('transfer-freeze.json');
assert.equal(input.buildId, 'b6caec6c-117e-49eb-86fa-4182e64d5b55');
assert.equal(input.sourceSha256, '60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640');
assert.equal(input.requestedDisplay, '1024x768');
assert.equal(input.appDeadlineMs, 120000);
assert.equal(freeze.buildId, input.buildId);
assert.equal(sha256(fs.readFileSync(path.join(raw, 'transfer-run-display-explore.ps1'))), input.guestScriptSha256);
for (const gateName of ['gate.json', 'gate-before-launch.json'])
  assert.deepEqual(parse(gateName).processes, [], `${gateName}: external process`);
const launch = parse('launch.json');
assert.equal(launch.pid, 36196);
assert.equal(launch.freezeSha256, expectedFreeze);
assert(launch.arguments.includes('-snapshot'));
assert(launch.arguments.includes('none'));
const send = parse('command-send.json');
assert.equal(send.pid, launch.pid);
assert.equal(send.command, 'cmd.exe /c D:\\soui-input\\run-display-explore.cmd');
assert.equal(manifest.filter(item => item.name.startsWith('monitor-run-')).length, 1);
assert.equal(manifest.filter(item => item.name.startsWith('monitor-hmp-')).length, 1);
assert.equal(manifest.filter(item => item.name.startsWith('monitor-screen-')).length, 5);

const result = parse('qemu-result.json');
assert.equal(result.pid, launch.pid);
assert.equal(result.exitCode, 0);
assert.equal(result.timedOut, false);
assert.equal(result.externalLoadObserved, false);
assert.equal(result.gateFailure, null);
assert.equal(result.frozenCheckExitCode, 0);
assert.equal(result.diskUnchanged, true);
assert.equal(result.diskSha256Before, 'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');
assert.equal(result.diskSha256After, result.diskSha256Before);
const samples = parse('gates-during.json');
assert.equal(samples.length, 95);
assert(samples.every(sample => sample.error === null && Array.isArray(sample.processes) && sample.processes.length === 0));
assert.equal(samples.at(-1).phase, 'after-qemu-exit');
assert(new Date(samples.at(-1).at) >= new Date(result.qemuExitedAt));
assert(new Date(samples.at(-1).at) - new Date(result.qemuExitedAt) <= 15000);

const serial = fs.readFileSync(path.join(raw, 'serial-export.txt'));
assert(serial.toString('utf8').trimEnd().endsWith('SOUI_SERIAL_END'));
const review = parse('review-v11.json');
assert.equal(review.serialSha256, sha256(serial));
assert.equal(review.serialBytes, serial.length);
assert.equal(review.serialFrames, 54);
assert.equal(review.qemuPid, launch.pid);
assert.equal(review.gateSamples, samples.length);
assert.equal(review.guestIdentityVerified, true);
assert.equal(review.displayMode1024x768Verified, true);
assert.equal(review.payloadPrePostVerified, true);
assert.equal(review.applicationNativeExitCode, 0);
assert.equal(review.closeWasHarnessPosted, true);
assert.deepEqual(review.initialBounds, [87, 24, 937, 664]);
assert.deepEqual(review.resizeBefore, review.initialBounds);
assert.deepEqual(review.resizeAfter, [87, 24, 1017, 724]);
assert.deepEqual(review.postResizeBounds, review.resizeAfter);
assert.equal(review.nativeResizeVerified, true);
assert.equal(review.screenshotReviewed, false);
for (const field of ['realScrollVerified', 'imeCompositionVerified', 'taskParallelVerified',
  'win7CurrentV02Verified', 'benchmarkValid', 'releaseReady']) assert.equal(review[field], false, field);
for (const name of ['driver-result.json', 'review-result.json']) {
  const command = parse(name);
  assert.equal(command.exitCode, 0, name);
  assert.equal(command.timedOut, false, name);
  assert.equal(command.error, null, name);
}
assert.equal(fs.statSync(path.join(raw, 'driver-stderr.log')).size, 0);
assert.equal(fs.statSync(path.join(raw, 'review-stderr.log')).size, 0);
assert(fs.statSync(path.join(raw, 'desktop-aftercommand-2.ppm')).size > 100000);
console.log(JSON.stringify({status: 'verified', rawFiles: manifest.length,
  originalBytesMatched: Boolean(originRoot), guestIdentityVerified: true,
  serialFrames: review.serialFrames, nativeResizeVerified: true,
  initialBounds: review.initialBounds, resizeAfter: review.resizeAfter,
  win7CurrentV02Verified: false, releaseReady: false}));
