'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const commands = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
const json = file => JSON.parse(read(file).toString('utf8').replace(/^\uFEFF/, ''));
const freezePath = path.join(root, 'display-explore-attempt1/freeze.json');
const input = json(path.join(root, 'display-explore-attempt1/input.json'));
const freeze = json(freezePath);
assert.equal(sha(read(freezePath)), '3dcc7c9eb1dbf71fd79f1626e32192a3e4bcab020612b3a27ab6ab557782474b');
assert.equal(freeze.files.length, 13);
assert.equal(input.transferFiles.length, 11);
assert.equal(input.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(input.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
const exe = path.join(root, 'display-probe-build-attempt1/Release/V02DisplayModeProbe.exe');
assert.equal(sha(read(exe)), input.displayProbeSha256);
assert.equal(input.displayProbeSha256, '9fe2a3c5ca8ab983c7cd01338b25a6b1d80844c6637f8ef94403d850a17708a2');
const project = read(path.join(root, 'display-probe-build-attempt1/V02DisplayModeProbe.vcxproj')).toString('utf8');
assert.match(project, /<RuntimeLibrary>MultiThreaded<\/RuntimeLibrary>/);
const names = ['win7-display-probe-configure-attempt1', 'win7-display-probe-build-attempt1',
  'win7-display-probe-dependents-attempt1', 'win7-display-probe-headers-attempt1'];
const commandRecords = names.map(name => {
  const result = json(path.join(commands, name, 'result.json'));
  assert.equal(result.exitCode, 0, name);
  assert.equal(result.timedOut, false, name);
  assert.equal(result.error, null, name);
  assert.equal(result.interruptedByExternal ?? null, null, name);
  return {name, resultSha256: sha(read(path.join(commands, name, 'result.json')))};
});
const dependencies = read(path.join(commands, 'win7-display-probe-dependents-attempt1/stdout.log')).toString('utf8');
assert.match(dependencies, /USER32\.dll/);
assert.match(dependencies, /KERNEL32\.dll/);
assert.doesNotMatch(dependencies, /VCRUNTIME|MSVCP140|api-ms-win-crt/i);
const headers = read(path.join(commands, 'win7-display-probe-headers-attempt1/stdout.log')).toString('utf8');
assert.match(headers, /14C machine \(x86\)/);
assert.match(headers, /6\.00 subsystem version/);
const priorAttempt = path.join(root, 'guest-visible-explore-attempt1');
const priorGate = json(path.join(priorAttempt, 'gate-before-launch.json'));
assert(priorGate.processes.some(process => process.Name === 'qemu-system-x86_64.exe'));
assert.equal(fs.existsSync(path.join(priorAttempt, 'launch.json')), false);
assert.equal(fs.existsSync(path.join(priorAttempt, 'command-send.json')), false);
assert.equal(fs.existsSync(path.join(priorAttempt, 'serial-export.txt')), false);
const review = {
  schema: 'soui.win7-v02-current-display-preparation-review/v1',
  sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: sha(read(freezePath)), frozenFiles: freeze.files.length,
  transferFiles: input.transferFiles.length, displayProbeSha256: input.displayProbeSha256,
  displayProbeArchitecture: 'x86', displayProbeSubsystemVersion: '6.00',
  displayProbeDirectDependencies: ['USER32.dll', 'KERNEL32.dll'],
  commandRecords, previousVisibleAttemptStoppedBeforeOwnQemu: true,
  guestDisplayTestExecuted: false, dataPageVisibleVerified: false,
  win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(root, 'display-preparation-review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
