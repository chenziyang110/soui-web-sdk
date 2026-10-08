'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const attemptName = 'guest-display-explore-attempt3';
const attempt = path.join(root, attemptName);
const frozen = path.join(root, 'display-explore-attempt1');
const commandRoot = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = name => fs.readFileSync(path.join(attempt, name));
const json = name => JSON.parse(read(name).toString('utf8').replace(/^\uFEFF/, ''));
const freeze = fs.readFileSync(path.join(frozen, 'freeze.json'));
const input = JSON.parse(fs.readFileSync(path.join(frozen, 'input.json'), 'utf8'));
assert.equal(sha(freeze), '3dcc7c9eb1dbf71fd79f1626e32192a3e4bcab020612b3a27ab6ab557782474b');
assert.equal(input.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(input.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
const launch = json('launch.json');
const sent = json('command-send.json');
const qemu = json('qemu-result.json');
const driver = JSON.parse(fs.readFileSync(path.join(commandRoot, `${attemptName}-driver/result.json`), 'utf8'));
const strictFailure = JSON.parse(fs.readFileSync(path.join(commandRoot, `${attemptName}-review-attempt1/result.json`), 'utf8'));
assert.equal(driver.exitCode, 0);
assert.equal(strictFailure.exitCode, 1);
assert.equal(launch.freezeSha256, sha(freeze));
assert.equal(launch.pid, sent.pid);
assert.equal(sent.command, input.guestCommand);
assert.equal(launch.monitorScriptSha256, sha(read('monitor-script.cjs')));
assert(launch.arguments.includes('-snapshot'));
assert(launch.arguments.includes('-net') && launch.arguments.includes('none'));
assert(launch.arguments.some(argument => argument.startsWith('file=fat:ro:')));
assert.equal(qemu.pid, launch.pid);
assert.equal(qemu.exitCode, 0);
assert.equal(qemu.timedOut, false);
assert.equal(qemu.externalLoadObserved, false);
assert.equal(qemu.gateFailure, null);
assert.equal(qemu.diskUnchanged, true);
assert.equal(qemu.frozenCheckExitCode, 0);
assert.equal(qemu.diskSha256Before, 'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');
assert.equal(qemu.diskSha256After, qemu.diskSha256Before);
const gatesValue = json('gates-during.json');
const gates = Array.isArray(gatesValue) ? gatesValue : [gatesValue];
assert(gates.length > 0);
assert(gates.every(sample => !sample.error && (!sample.processes || sample.processes.length === 0)));
const serial = read('serial-export.txt');
const encoded = serial.toString('ascii');
assert(Buffer.from(encoded, 'ascii').equals(serial));
const lines = encoded.split(/\r?\n/);
assert.equal(lines.shift(), 'SOUI_SERIAL_V1');
assert.equal(lines.pop(), '');
assert.equal(lines.pop(), 'SOUI_SERIAL_END');
assert.equal(lines.length % 2, 0);
const files = new Map();
for (let index = 0; index < lines.length; index += 2) {
  const header = /^FILE\|([a-z0-9][a-z0-9._-]*)\|([0-9]+)\|([A-F0-9]{64})$/.exec(lines[index]);
  assert(header, `Invalid frame ${index}`);
  assert.equal(files.has(header[1]), false);
  const base64 = lines[index + 1];
  assert.match(base64, /^[A-Za-z0-9+/]*={0,2}$/);
  const value = Buffer.from(base64, 'base64');
  assert.equal(value.toString('base64'), base64);
  assert.equal(value.length, Number(header[2]));
  assert.equal(sha(value).toUpperCase(), header[3]);
  files.set(header[1], value);
}
assert.equal(files.size, 24);
const text = name => {
  assert(files.has(name), `Missing ${name}`);
  const value = files.get(name);
  return value[0] === 0xff && value[1] === 0xfe
    ? value.subarray(2).toString('utf16le')
    : value.toString('utf8').replace(/^\uFEFF/, '');
};
assert.match(text('guest-identity.txt'), /Version=6\.1\.7601/);
assert.match(text('guest-identity.txt'), /ServicePackMajorVersion=1/);
assert.match(text('guest-identity.txt'), /OSArchitecture=32-bit/);
assert.match(text('guest-identity.txt'), /PointerBytes=4/);
assert.match(text('guest-identity.txt'), /PowerShellMajor=2/);
assert.equal(text('display-mode-exitcode.txt').trim(), '0');
assert.match(text('display-mode.log'), /after width=1024 height=768 .*metrics=1024x768/);
for (const name of ['starter-zip-test-exitcode.txt', 'starter-extract-exitcode.txt',
  'workbench-zip-test-exitcode.txt', 'workbench-extract-exitcode.txt', 'payload-pre-exitcode.txt'])
  assert.equal(text(name).trim(), '0', name);
assert.equal(text('run-exitcode.txt').trim(), '1');
assert.equal(text('harness-exception.txt').trim(), 'visible unowned top-level target window missing');
assert.match(text('harness.log'), /transfer verified[\s\S]*1024x768 display verified[\s\S]*payload pre verified[\s\S]*failed: visible unowned top-level target window missing/);
assert.equal(text('window-inspect-command.log').length, 0);
for (const name of ['run-complete.txt', 'window-inspect.json', 'window-inspect-exitcode.txt',
  'workbench-visible-explore-run.txt', 'workbench-visible-explore-exitcode.txt',
  'payload-post-exitcode.txt']) assert.equal(files.has(name), false, name);
assert.equal(sha(files.get('transfer-manifest-raw.tsv')), input.transferManifestSha256);
const observed = text('transfer-observed.tsv').trim().split(/\r?\n/).map(line => {
  const fields = line.split('\t');
  assert.equal(fields.length, 3);
  return {sha256: fields[0], bytes: Number(fields[1]), path: fields[2]};
});
const transferManifest = fs.readFileSync(path.join(frozen, 'volume/soui-input/transfer-files.tsv'));
assert.deepEqual(observed.sort((a, b) => a.path.localeCompare(b.path)),
  [...input.transferFiles, {sha256: sha(transferManifest), bytes: transferManifest.length, path: 'transfer-files.tsv'}]
    .sort((a, b) => a.path.localeCompare(b.path)));
for (const app of ['SouiStarter', 'SouiWorkbench']) {
  const expected = fs.readFileSync(path.join(frozen, 'volume/soui-input', `${app}-payload.tsv`), 'utf8')
    .replace(/^\uFEFF/, '').trim().split(/\r?\n/);
  assert.equal(expected.length, app === 'SouiStarter' ? 71 : 475);
  assert.equal(expected.filter(line => line.split('\t')[2].startsWith('resources/legal/')).length, 5);
  assert.deepEqual(text(`${app.toLowerCase()}-payload-pre.tsv`).trim().split(/\r?\n/), expected);
}
const review = {
  schema: 'soui.win7-v02-current-display-failure-review/v2',
  attempt: attemptName,
  sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: sha(freeze), serialSha256: sha(serial), serialBytes: serial.length,
  serialFrames: files.size, qemuPid: launch.pid, gateSamples: gates.length,
  guestIdentityVerified: true, transferVerified: true, completePayloadsAndLegalPreVerified: true,
  displayMode1024x768Verified: true, displayModeNativeExitCode: 0,
  applicationWindowInspectionError: 'visible unowned top-level target window missing',
  applicationNativeExitCode: null, applicationNaturalCloseVerified: false,
  dataPageVisibleVerified: false, v02RealInputVerified: false,
  win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'display-failure-review-v2.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
