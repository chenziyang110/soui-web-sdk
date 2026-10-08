'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const name = process.argv[2];
assert.match(name || '', /^guest-layout-interaction-v3-attempt[0-9]+$/);
assert.match(process.argv[3] || '', /^[a-f0-9]{64}$/);
const attempt = path.join(root, name);
const frozen = path.join(root, 'layout-interaction-v3-attempt1');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(path.join(attempt, file));
const json = file => JSON.parse(read(file).toString('utf8').replace(/^\uFEFF/, ''));
const freezeBytes = fs.readFileSync(path.join(frozen, 'freeze.json'));
const input = JSON.parse(fs.readFileSync(path.join(frozen, 'input.json'), 'utf8'));
assert.equal(sha(freezeBytes), process.argv[3]);
assert.equal(input.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(input.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(input.expectedSerialFrames, 98);
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
const gateValue = json('gates-during.json');
const gates = Array.isArray(gateValue) ? gateValue : [gateValue];
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
assert.equal(files.size, input.expectedSerialFrames);
assert(files.size <= input.serialFrameLimit);
const text = file => {
  assert(files.has(file), `Missing ${file}`);
  const bytes = files.get(file);
  return bytes[0] === 0xff && bytes[1] === 0xfe
    ? bytes.subarray(2).toString('utf16le')
    : bytes.toString('utf8').replace(/^\uFEFF/, '');
};
for (const file of ['guest-identity.txt', 'harness.log', 'run-complete.txt', 'run-exitcode.txt',
  'display-mode.log', 'display-mode-exitcode.txt', 'window-inspect.json',
  'window-inspect-samples.txt', 'workbench-visible-explore-run.txt',
  'workbench-visible-explore-exitcode.txt', 'close-attempt.txt',
  'payload-pre-exitcode.txt', 'payload-post-exitcode.txt',
  'starter-zip-test-exitcode.txt', 'starter-extract-exitcode.txt',
  'workbench-zip-test-exitcode.txt', 'workbench-extract-exitcode.txt',
  'transfer-manifest-raw.tsv', 'transfer-observed.tsv',
  'interaction-baseline-ready.txt', 'interaction-dispatch-start.txt',
  'task-click-before.txt', 'interaction-dispatch-finish.txt']) text(file);
for (const pattern of [/Version=6\.1\.7601/, /ServicePackMajorVersion=1/,
  /OSArchitecture=32-bit/, /PointerBytes=4/, /PowerShellMajor=2/])
  assert.match(text('guest-identity.txt'), pattern);
for (const file of ['run-exitcode.txt', 'display-mode-exitcode.txt',
  'workbench-visible-explore-exitcode.txt', 'payload-pre-exitcode.txt',
  'payload-post-exitcode.txt', 'starter-zip-test-exitcode.txt',
  'starter-extract-exitcode.txt', 'workbench-zip-test-exitcode.txt',
  'workbench-extract-exitcode.txt']) assert.equal(text(file).trim(), '0', file);
assert.match(text('display-mode.log'), /after width=1024 height=768 .*metrics=1024x768/);
assert.match(text('workbench-visible-explore-run.txt'),
  /Arguments=--arch=x86 --renderer=cpu --react --page=data --width=850 --height=640/);
assert.match(text('workbench-visible-explore-run.txt'),
  /TimeoutMs=120000\r?\nTimedOut=False\r?\nRemainingProcess=False\r?\nExitCode=0/);
assert.match(text('close-attempt.txt'), /CloseMainWindowPosted=True/);
assert.match(text('run-complete.txt'), /buildId=f0c38056-058a-456e-be99-17446127f7ec/);
assert.match(text('run-complete.txt'),
  /source=15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d;apps=1;status=completed;scope=1024x768-v02-real-ime-resize-task-exploration-not-production/);
assert.equal(sha(files.get('transfer-manifest-raw.tsv')), input.transferManifestSha256);
const observed = text('transfer-observed.tsv').trim().split(/\r?\n/).map(line => {
  const fields = line.split('\t'); assert.equal(fields.length, 3);
  return {sha256: fields[0], bytes: Number(fields[1]), path: fields[2]};
});
const transfer = path.join(frozen, 'volume/soui-input');
const manifest = fs.readFileSync(path.join(transfer, 'transfer-files.tsv'));
assert.deepEqual(observed.sort((a, b) => a.path.localeCompare(b.path)),
  [...input.transferFiles, {sha256: sha(manifest), bytes: manifest.length, path: 'transfer-files.tsv'}]
    .sort((a, b) => a.path.localeCompare(b.path)));
for (const app of ['SouiStarter', 'SouiWorkbench']) {
  const payload = fs.readFileSync(path.join(transfer, `${app}-payload.tsv`), 'utf8')
    .replace(/^\uFEFF/, '').trim().split(/\r?\n/);
  assert.equal(payload.length, app === 'SouiStarter' ? 71 : 475);
  assert.equal(payload.filter(line => line.split('\t')[2].startsWith('resources/legal/')).length, 5);
  for (const phase of ['pre', 'post'])
    assert.deepEqual(text(`${app.toLowerCase()}-payload-${phase}.tsv`).trim().split(/\r?\n/), payload);
}
const inspect = JSON.parse(text('window-inspect.json'));
const pid = Number(/^PID=([0-9]+)$/m.exec(text('workbench-visible-explore-run.txt'))?.[1]);
assert.equal(inspect.schema, 'soui.v02.os-input/v1');
assert.equal(inspect.action, 'inspect');
assert.equal(inspect.pid, pid);
assert(inspect.hwnd > 0);
assert.equal(inspect.releaseReady, false);
assert.deepEqual(inspect.before, inspect.after);
const [left, top, right, bottom] = inspect.before;
assert(left >= 0 && top >= 0 && right <= 1024 && bottom <= 768);
const creation = /"creationFiletime":([0-9]+)/.exec(text('window-inspect.json'))?.[1];
assert(creation);
const actionPlan = [
  ...Array.from({length: 12}, (_, index) => [`down-${index + 1}`, 'wheel']),
  ['start-task', 'click'],
  ...Array.from({length: 4}, (_, index) => [`up-${index + 1}`, 'wheel-up']),
  ['focus-input', 'click'], ['pinyin', 'pinyin'], ['commit-space', 'key-space'],
  ['resize', 'resize-drag'],
  ...Array.from({length: 8}, (_, index) => [`return-${index + 1}`, 'wheel-up']),
  ['inner-list-wheel', 'wheel'],
];
assert.equal(actionPlan.length, 30);
for (const [label, action] of actionPlan) {
  const raw = text(`action-${label}.json`);
  const value = JSON.parse(raw);
  assert.equal(value.schema, 'soui.v02.os-input/v1');
  assert.equal(value.pid, pid);
  assert.equal(value.hwnd, inspect.hwnd);
  assert.equal(value.action, action);
  assert.equal(value.foregroundBefore, true);
  assert.equal(/"creationFiletime":([0-9]+)/.exec(raw)?.[1], creation);
  assert.match(text(`action-${label}-process.txt`), /ExitCode=0\r?\nTimedOut=False/);
  if (label === 'resize') {
    const [oldLeft, oldTop, oldRight, oldBottom] = value.before;
    const [newLeft, newTop, newRight, newBottom] = value.after;
    assert(newRight - newLeft > oldRight - oldLeft);
    assert(newBottom - newTop > oldBottom - oldTop);
    assert(newLeft >= 0 && newTop >= 0 && newRight <= 1024 && newBottom <= 768);
  }
}
const elapsed = name => Number(/^ElapsedMs=([0-9]+)$/m.exec(text(name))?.[1]);
const baselineMs = elapsed('interaction-baseline-ready.txt');
const startMs = elapsed('interaction-dispatch-start.txt');
const taskClickMs = elapsed('task-click-before.txt');
const finishMs = elapsed('interaction-dispatch-finish.txt');
const closeMs = Number(/ElapsedMs=([0-9]+);MainWindowHandle=.*;CloseMainWindowPosted=True/
  .exec(text('close-attempt.txt'))?.[1]);
assert(Number.isFinite(baselineMs) && baselineMs >= 10000 && baselineMs < 55000);
assert(startMs - baselineMs >= 8000);
assert(taskClickMs > startMs && finishMs > taskClickMs);
assert(closeMs > finishMs && closeMs < 115000);
const review = {
  schema: 'soui.win7-v02-current-interaction-review/v3',
  sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: sha(freezeBytes), serialSha256: sha(serial),
  serialBytes: serial.length, serialFrames: files.size,
  qemuPid: launch.pid, gateSamples: gates.length,
  guestIdentity: text('guest-identity.txt').trim(),
  applicationPid: pid, applicationHwnd: inspect.hwnd,
  initialWindowInsideDisplay: true, completePayloadsAndLegalPrePost: true,
  applicationNativeExitCode: 0, naturalClose: true,
  nativeInputActionsSent: actionPlan.length,
  resizeBoundsChecked: true,
  baselineElapsedMs: baselineMs, dispatchStartElapsedMs: startMs,
  taskClickElapsedMs: taskClickMs, dispatchFinishElapsedMs: finishMs,
  closeElapsedMs: closeMs,
  taskStartedVerified: false, imeCompositionVerified: false,
  taskParallelVerified: false, visualScreenshotReviewed: false,
  win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'interaction-review-v3.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
