'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const name = process.argv[2];
assert.match(name || '', /^guest-display-explore-attempt[0-9]+$/);
const attempt = path.join(root, name);
const frozen = path.join(root, 'display-explore-attempt1');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(path.join(attempt, file));
const json = file => JSON.parse(read(file).toString('utf8').replace(/^\uFEFF/, ''));
const freezeBytes = fs.readFileSync(path.join(frozen, 'freeze.json'));
const input = JSON.parse(fs.readFileSync(path.join(frozen, 'input.json'), 'utf8'));
assert.equal(sha(freezeBytes), '3dcc7c9eb1dbf71fd79f1626e32192a3e4bcab020612b3a27ab6ab557782474b');
assert.equal(input.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(input.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
const launch = json('launch.json');
const command = json('command-send.json');
const result = json('qemu-result.json');
assert.equal(launch.freezeSha256, sha(freezeBytes));
assert.equal(launch.pid, command.pid);
assert.equal(command.command, input.guestCommand);
assert.equal(launch.monitorScriptSha256, sha(read('monitor-script.cjs')));
assert(launch.arguments.includes('-snapshot'));
assert(launch.arguments.includes('-net') && launch.arguments.includes('none'));
assert(launch.arguments.some(value => value.startsWith('file=fat:ro:')));
assert.equal(result.pid, launch.pid);
assert.equal(result.exitCode, 0);
assert.equal(result.timedOut, false);
assert.equal(result.externalLoadObserved, false);
assert.equal(result.gateFailure, null);
assert.equal(result.diskUnchanged, true);
assert.equal(result.frozenCheckExitCode, 0);
assert.equal(result.diskSha256Before, 'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');
assert.equal(result.diskSha256After, result.diskSha256Before);
assert(new Date(launch.startedAt).getTime() <= new Date(command.sentAt).getTime());
assert(new Date(command.sentAt).getTime() <= new Date(result.finishedAt).getTime());
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
  assert(header, `Invalid serial header ${index}`);
  assert.equal(files.has(header[1]), false);
  const base64 = lines[index + 1];
  assert.match(base64, /^[A-Za-z0-9+/]*={0,2}$/);
  const value = Buffer.from(base64, 'base64');
  assert.equal(value.toString('base64'), base64);
  assert.equal(value.length, Number(header[2]));
  assert.equal(sha(value).toUpperCase(), header[3]);
  files.set(header[1], value);
}
assert(files.size <= 40);
const text = file => { assert(files.has(file), `Missing ${file}`); return files.get(file).toString('utf8').replace(/^\uFEFF/, ''); };
for (const file of ['guest-identity.txt', 'harness.log', 'run-complete.txt', 'run-exitcode.txt',
  'display-mode.log', 'display-mode-exitcode.txt', 'window-inspect.json', 'window-inspect-exitcode.txt',
  'workbench-visible-explore-run.txt', 'workbench-visible-explore-exitcode.txt', 'close-attempt.txt',
  'payload-pre-exitcode.txt', 'payload-post-exitcode.txt',
  'starter-zip-test-exitcode.txt', 'starter-extract-exitcode.txt',
  'workbench-zip-test-exitcode.txt', 'workbench-extract-exitcode.txt',
  'transfer-manifest-raw.tsv', 'transfer-observed.tsv']) text(file);
for (const pattern of [/Version=6\.1\.7601/, /ServicePackMajorVersion=1/,
  /OSArchitecture=32-bit/, /PointerBytes=4/, /PowerShellMajor=2/])
  assert.match(text('guest-identity.txt'), pattern);
for (const file of ['run-exitcode.txt', 'display-mode-exitcode.txt', 'window-inspect-exitcode.txt',
  'workbench-visible-explore-exitcode.txt', 'payload-pre-exitcode.txt', 'payload-post-exitcode.txt',
  'starter-zip-test-exitcode.txt', 'starter-extract-exitcode.txt',
  'workbench-zip-test-exitcode.txt', 'workbench-extract-exitcode.txt'])
  assert.equal(text(file).trim(), '0', file);
assert.match(text('display-mode.log'), /after width=1024 height=768 .*metrics=1024x768/);
assert.match(text('workbench-visible-explore-run.txt'), /Arguments=--arch=x86 --renderer=cpu --react --page=data --width=700 --height=500/);
assert.match(text('workbench-visible-explore-run.txt'), /TimeoutMs=60000\r?\nTimedOut=False\r?\nRemainingProcess=False\r?\nExitCode=0/);
assert.match(text('close-attempt.txt'), /CloseMainWindowPosted=True/);
assert.match(text('run-complete.txt'), /buildId=f0c38056-058a-456e-be99-17446127f7ec/);
assert.match(text('run-complete.txt'), /source=15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d;apps=1;status=completed;scope=1024x768-visible-route-exploration-not-v02-input/);
assert.equal(sha(files.get('transfer-manifest-raw.tsv')), input.transferManifestSha256);
const observed = text('transfer-observed.tsv').trim().split(/\r?\n/).map(line => {
  const fields = line.split('\t'); assert.equal(fields.length, 3);
  return {sha256: fields[0], bytes: Number(fields[1]), path: fields[2]};
});
const manifest = fs.readFileSync(path.join(frozen, 'volume/soui-input/transfer-files.tsv'));
assert.deepEqual(observed.sort((a, b) => a.path.localeCompare(b.path)),
  [...input.transferFiles, {sha256: sha(manifest), bytes: manifest.length, path: 'transfer-files.tsv'}]
    .sort((a, b) => a.path.localeCompare(b.path)));
for (const app of ['SouiStarter', 'SouiWorkbench']) {
  const lines = fs.readFileSync(path.join(frozen, 'volume/soui-input', `${app}-payload.tsv`), 'utf8')
    .replace(/^\uFEFF/, '').trim().split(/\r?\n/);
  assert.equal(lines.length, app === 'SouiStarter' ? 71 : 475);
  assert.equal(lines.filter(line => line.split('\t')[2].startsWith('resources/legal/')).length, 5);
  for (const phase of ['pre', 'post'])
    assert.deepEqual(text(`${app.toLowerCase()}-payload-${phase}.tsv`).trim().split(/\r?\n/), lines);
}
const inspect = JSON.parse(text('window-inspect.json'));
const appRun = text('workbench-visible-explore-run.txt');
const pid = Number(/^PID=([0-9]+)$/m.exec(appRun)?.[1]);
assert.equal(inspect.schema, 'soui.v02.os-input/v1');
assert.equal(inspect.action, 'inspect');
assert.equal(inspect.pid, pid);
assert(inspect.hwnd > 0);
assert.equal(inspect.releaseReady, false);
assert(Array.isArray(inspect.before) && inspect.before.length === 4);
assert.deepEqual(inspect.before, inspect.after);
const [left, top, right, bottom] = inspect.before;
assert(right > left && bottom > top);
const wholeWindowInsideDisplay = left >= 0 && top >= 0 && right <= 1024 && bottom <= 768;
const review = {
  schema: 'soui.win7-v02-current-display-explore-review/v1',
  sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: sha(freezeBytes), serialSha256: sha(serial), serialBytes: serial.length,
  serialFrames: files.size, qemuPid: launch.pid, gateSamples: gates.length,
  guestIdentity: text('guest-identity.txt').trim(), displayMode: '1024x768',
  displayModeNativeExitCode: 0, applicationNativeExitCode: 0,
  applicationPid: pid, applicationHwnd: inspect.hwnd,
  foregroundAtInspect: inspect.foregroundBefore, wholeWindowInsideDisplay,
  completePayloadsAndLegalPrePost: true, naturalClose: true,
  visualScreenshotReviewed: false, dataPageVisibleVerified: false,
  v02RealInputVerified: false, win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'display-review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
