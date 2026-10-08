'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const name = 'guest-display-explore-v2-attempt1';
const attempt = path.join(root, name);
const command = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/guest-display-explore-v2-attempt1-driver';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const json = name => JSON.parse(fs.readFileSync(path.join(attempt, name), 'utf8').replace(/^\uFEFF/, ''));
const frozen = path.join(root, 'display-explore-v2-attempt1', 'freeze.json');
const freezeBytes = fs.readFileSync(frozen);
assert.equal(sha(freezeBytes), 'df9f2131ee3c9a99823d577094f16572187c038e57eaca83da723a1ad5db11e4');
const launch = json('launch.json');
const result = json('qemu-result.json');
const driver = JSON.parse(fs.readFileSync(path.join(command, 'result.json'), 'utf8'));
assert.equal(driver.exitCode, 1);
assert.equal(driver.timedOut, false);
assert.equal(driver.error, null);
assert.equal(launch.freezeSha256, sha(freezeBytes));
assert.equal(launch.pid, result.pid);
assert.equal(result.exitCode, -1);
assert.equal(result.timedOut, false);
assert.equal(result.externalLoadObserved, true);
assert.equal(result.gateFailure, null);
assert.equal(result.diskUnchanged, true);
assert.equal(result.diskSha256Before, 'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');
assert.equal(result.diskSha256After, result.diskSha256Before);
assert.equal(result.frozenCheckExitCode, 0);
assert.equal(fs.readFileSync(path.join(attempt, 'serial-export.txt')).length, 0);
assert.equal(fs.existsSync(path.join(attempt, 'command-send.json')), false);
const gatesValue = json('gates-during.json');
const gates = Array.isArray(gatesValue) ? gatesValue : [gatesValue];
assert(gates.length > 0);
const external = gates.flatMap(gate => gate.processes || []);
assert(external.length > 0);
assert(external.some(process => process.ProcessId !== launch.pid));
const review = {
  schema: 'soui.win7-v02-display-v2-interruption-review/v1',
  attempt: name, sourceSha256: '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d',
  buildId: 'f0c38056-058a-456e-be99-17446127f7ec',
  freezeSha256: sha(freezeBytes), ownQemuPid: launch.pid,
  externalProcessIdsObserved: [...new Set(external.map(process => process.ProcessId))],
  gateSamples: gates.length, ownQemuExitCode: -1, driverExitCode: 1,
  serialBytes: 0, guestCommandSent: false, applicationStarted: false,
  diskAndFrozenInputUnchanged: true, win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'interrupted-review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
