'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const attempt = path.join(root, 'guest-display-explore-attempt1');
const command = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/guest-display-explore-attempt1-driver';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
const json = file => JSON.parse(read(file).toString('utf8').replace(/^\uFEFF/, ''));
const launch = json(path.join(attempt, 'launch.json'));
const qemu = json(path.join(attempt, 'qemu-result.json'));
const driver = json(path.join(command, 'result.json'));
const gatesValue = json(path.join(attempt, 'gates-during.json'));
const gates = Array.isArray(gatesValue) ? gatesValue : [gatesValue];
assert.equal(launch.freezeSha256, '3dcc7c9eb1dbf71fd79f1626e32192a3e4bcab020612b3a27ab6ab557782474b');
assert.equal(qemu.pid, launch.pid);
assert.equal(qemu.exitCode, -1);
assert.equal(qemu.timedOut, false);
assert.equal(qemu.externalLoadObserved, true);
assert.equal(qemu.gateFailure, null);
assert.equal(qemu.frozenCheckExitCode, 0);
assert.equal(qemu.diskUnchanged, true);
assert.equal(qemu.diskSha256Before, 'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');
assert.equal(qemu.diskSha256After, qemu.diskSha256Before);
assert.equal(driver.exitCode, 1);
assert.equal(driver.timedOut, false);
assert.equal(driver.error, null);
assert(gates.length >= 2);
assert(gates.slice(0, -1).every(gate => !gate.error && gate.processes.length === 0));
const last = gates.at(-1);
assert.equal(last.error, null);
assert.equal(last.processes.length, 1);
assert.equal(last.processes[0].Name, 'qemu-system-x86_64.exe');
assert.notEqual(last.processes[0].ProcessId, launch.pid);
assert(new Date(launch.startedAt).getTime() < new Date(last.at).getTime());
assert(new Date(last.at).getTime() <= new Date(qemu.finishedAt).getTime());
assert.equal(fs.existsSync(path.join(attempt, 'command-send.json')), false);
assert.equal(fs.existsSync(path.join(attempt, 'display-review.json')), false);
assert.equal(read(path.join(attempt, 'serial-export.txt')).length, 0);
const screenshot = read(path.join(attempt, 'boot-01.ppm'));
assert.match(screenshot.subarray(0, 40).toString('ascii'), /^P6\s+800 600\s+255\s/);
const review = {
  schema: 'soui.win7-v02-display-explore-interrupted-review/v1',
  attempt: 'guest-display-explore-attempt1',
  freezeSha256: launch.freezeSha256, qemuPid: launch.pid,
  driverExitCode: driver.exitCode, qemuExitCode: qemu.exitCode,
  externalQemuPid: last.processes[0].ProcessId, externalQemuFirstObservedAt: last.at,
  gateSamples: gates.length, bootScreenshotSha256: sha(screenshot),
  baseDiskUnchanged: true, frozenInputUnchanged: true,
  guestCommandSent: false, serialBytes: 0, displayModeExecuted: false,
  applicationStarted: false, win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'interrupted-review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
