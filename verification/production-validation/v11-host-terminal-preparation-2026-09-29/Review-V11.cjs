'use strict';

// Review one completed, immutable V11 guest attempt. Never turns the focused
// resize probe into an IME, task, benchmark, or production verdict.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const [attemptName, transferName, expectedFreezeSha] = process.argv.slice(2);
assert.match(attemptName || '', /^guest-native-resize-v11-attempt[1-9][0-9]*$/);
assert.match(transferName || '', /^win7-v11-transfer-attempt[1-9][0-9]*$/);
assert.match(expectedFreezeSha || '', /^[a-f0-9]{64}$/);
const root = __dirname;
const attempt = path.join(root, attemptName);
const transfer = path.join(root, transferName);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = (base, name) => fs.readFileSync(path.join(base, name));
const decode = bytes => bytes[0] === 0xff && bytes[1] === 0xfe
  ? bytes.subarray(2).toString('utf16le')
  : bytes.toString('utf8').replace(/^\uFEFF/, '');
const json = (base, name) => JSON.parse(decode(read(base, name)));
const freezeBytes = read(transfer, 'freeze.json');
assert.equal(sha(freezeBytes), expectedFreezeSha);
const input = json(transfer, 'input.json');
const launch = json(attempt, 'launch.json');
const sent = json(attempt, 'command-send.json');
const result = json(attempt, 'qemu-result.json');
assert.equal(input.schema, 'soui.win7-v02-native-resize-v11-input/v1');
assert.equal(input.sourceSha256,
  '60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640');
assert.equal(input.buildId, 'b6caec6c-117e-49eb-86fa-4182e64d5b55');
assert.equal(input.mode, 'focused-native-work-area-resize-probe-not-production');
assert.equal(launch.inputName, transferName);
assert.equal(launch.freezeSha256, expectedFreezeSha);
assert.equal(launch.pid, sent.pid);
assert.equal(launch.pid, result.pid);
assert.equal(sent.command, input.guestCommand);
assert.equal(fs.readdirSync(attempt).filter(name => /^monitor-run-.*\.log$/.test(name)).length, 1);
assert.equal(result.exitCode, 0);
assert.equal(result.timedOut, false);
assert.equal(result.externalLoadObserved, false);
assert.equal(result.gateFailure, null);
assert.equal(result.diskUnchanged, true);
assert.equal(result.diskSha256Before,
  'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');
assert.equal(result.diskSha256After, result.diskSha256Before);
assert.equal(result.frozenCheckExitCode, 0);
for (const name of ['frozen-check-before.log', 'frozen-check-after.log']) {
  const check = json(attempt, name);
  assert.equal(check.status, 'verified');
  assert.equal(check.freezeSha256, expectedFreezeSha);
  assert.equal(check.releaseReady, false);
}
const gateRows = json(attempt, 'gates-during.json');
const gates = Array.isArray(gateRows) ? gateRows : [gateRows];
assert(gates.length >= 2);
assert(gates.every(gate => !gate.error && (!gate.processes || gate.processes.length === 0)));
const terminalGate = gates.at(-1);
assert.equal(terminalGate.phase, 'after-qemu-exit');
const terminalMs = Date.parse(terminalGate.at);
const qemuExitMs = Date.parse(result.qemuExitedAt);
assert(Number.isFinite(terminalMs) && Number.isFinite(qemuExitMs));
assert(terminalMs >= qemuExitMs && terminalMs - qemuExitMs <= 15000,
  'Missing timely terminal host interference sample');

const serial = read(attempt, 'serial-export.txt');
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
  const bytes = Buffer.from(lines[index + 1], 'base64');
  assert.equal(bytes.toString('base64'), lines[index + 1]);
  assert.equal(bytes.length, Number(header[2]));
  assert.equal(sha(bytes).toUpperCase(), header[3]);
  files.set(header[1], bytes);
}
assert(files.size >= input.minimumSerialFrames && files.size <= input.serialFrameLimit);
const text = name => {
  assert(files.has(name), `Missing ${name}`);
  return decode(files.get(name));
};
for (const pattern of [/Version=6\.1\.7601/, /ServicePackMajorVersion=1/,
  /OSArchitecture=32-bit/, /PointerBytes=4/, /PowerShellMajor=2/])
  assert.match(text('guest-identity.txt'), pattern);
assert.equal(text('display-mode-exitcode.txt').trim(), '0');
assert.match(text('display-mode.log'), /after width=1024 height=768 .*metrics=1024x768/);
assert.equal(sha(files.get('transfer-manifest-raw.tsv')), input.transferManifestSha256);
const transferObserved = text('transfer-observed.tsv').trim().split(/\r?\n/).map(line => {
  const fields = line.split('\t');
  assert.equal(fields.length, 3);
  return {sha256: fields[0], bytes: Number(fields[1]), path: fields[2]};
});
const manifest = read(path.join(transfer, 'volume/soui-input'), 'transfer-files.tsv');
assert.deepEqual(transferObserved.sort((a, b) => a.path.localeCompare(b.path)),
  [...input.transferFiles,
    {sha256: sha(manifest), bytes: manifest.length, path: 'transfer-files.tsv'}]
    .sort((a, b) => a.path.localeCompare(b.path)));
for (const phase of ['pre', 'post']) {
  assert.equal(text(`payload-${phase}-exitcode.txt`).trim(), '0');
  assert.match(text(`payload-${phase}.log`), /SouiStarter=71,legal=5,all-hashes=matched/);
  assert.match(text(`payload-${phase}.log`), /SouiWorkbench=475,legal=5,all-hashes=matched/);
}
for (const app of ['SouiStarter', 'SouiWorkbench']) {
  const payload = read(path.join(transfer, 'volume/soui-input'), `${app}-payload.tsv`)
    .toString('utf8').replace(/^\uFEFF/, '').trim().split(/\r?\n/);
  assert.equal(payload.length, app === 'SouiStarter' ? 71 : 475);
  assert.equal(payload.filter(line => line.split('\t')[2].startsWith('resources/legal/')).length, 5);
  for (const phase of ['pre', 'post'])
    assert.deepEqual(text(`${app.toLowerCase()}-payload-${phase}.tsv`).trim().split(/\r?\n/),
      payload);
}
assert.equal(text('run-exitcode.txt').trim(), '0');
assert.equal(text('workbench-native-resize-exitcode.txt').trim(), '0');
assert.match(text('run-complete.txt'), /status=completed;scope=1024x768-v02-native-work-area-resize-not-production/);
assert.match(text('run-complete.txt'), new RegExp(`buildId=${input.buildId};source=${input.sourceSha256}`));
const appRun = text('workbench-native-resize-run.txt');
assert.match(appRun, /Arguments=--arch=x86 --renderer=cpu --react --page=data --width=850 --height=640/);
assert.match(appRun, /TimeoutMs=120000\r?\nTimedOut=False\r?\nRemainingProcess=False\r?\nExitCode=0/);
assert.match(text('workbench-native-resize-smoke.log'), /window-constructed\r?\nhost-started/);
assert.match(text('harness.log'), /workbench-native-resize exit=0/);
const pid = Number(/^PID=([0-9]+)$/m.exec(appRun)?.[1]);
assert(Number.isSafeInteger(pid) && pid > 0);
const baseline = text('resize-baseline-ready.txt');
const baselineMs = Number(/^ElapsedMs=([0-9]+)$/m.exec(baseline)?.[1]);
const baselinePid = Number(/^PID=([0-9]+)$/m.exec(baseline)?.[1]);
const baselineCreation = /^CreationFiletime=([0-9]+)$/m.exec(baseline)?.[1];
const baselineFile = /^InspectFile=(window-inspect-[0-9]+\.json)$/m.exec(baseline)?.[1];
assert(Number.isFinite(baselineMs) && baselineMs >= 10000 && baselineMs <= 90000);
assert.equal(baselinePid, pid);
assert(baselineCreation && baselineFile);
const baselineRaw = text(baselineFile);
const initial = JSON.parse(baselineRaw);
assert.equal(initial.schema, 'soui.v02.os-input/v1');
assert.equal(initial.action, 'inspect');
assert.equal(initial.pid, pid);
assert(initial.hwnd > 0);
assert.equal(/"creationFiletime":([0-9]+)/.exec(baselineRaw)?.[1], baselineCreation);
assert.deepEqual(initial.before, initial.after);
const bounds = rect => {
  assert(Array.isArray(rect) && rect.length === 4);
  const [left, top, right, bottom] = rect;
  assert(rect.every(Number.isFinite));
  assert(left >= 0 && top >= 0 && right <= 1024 && bottom <= 729);
  assert(right > left && bottom > top);
};
bounds(initial.before);
const actionFiles = [...files.keys()].filter(name => /^action-.*\.json$/.test(name)).sort();
assert.deepEqual(actionFiles, ['action-after-resize.json', 'action-resize.json']);
const action = (label, expectedAction) => {
  const raw = text(`action-${label}.json`);
  const value = JSON.parse(raw);
  assert.equal(value.schema, 'soui.v02.os-input/v1');
  assert.equal(value.action, expectedAction);
  assert.equal(value.pid, pid);
  assert.equal(value.hwnd, initial.hwnd);
  assert.equal(value.foregroundBefore, true);
  assert.equal(/"creationFiletime":([0-9]+)/.exec(raw)?.[1], baselineCreation);
  assert.equal(value.releaseReady, false);
  assert.match(text(`action-${label}-process.txt`), /ExitCode=0\r?\nTimedOut=False/);
  bounds(value.before); bounds(value.after);
  return value;
};
const resized = action('resize', 'resize-drag');
const after = action('after-resize', 'inspect');
assert.deepEqual(resized.before, initial.before,
  'Window changed between the baseline inspection and native drag');
assert.equal(resized.after[0], resized.before[0], 'Resize moved the left edge');
assert.equal(resized.after[1], resized.before[1], 'Resize moved the top edge');
const widthDelta = (resized.after[2] - resized.after[0]) -
  (resized.before[2] - resized.before[0]);
const heightDelta = (resized.after[3] - resized.after[1]) -
  (resized.before[3] - resized.before[1]);
assert(widthDelta >= 64 && widthDelta <= 96,
  `Native 80-pixel drag changed width by ${widthDelta} pixels`);
assert(heightDelta >= 44 && heightDelta <= 76,
  `Native 60-pixel drag changed height by ${heightDelta} pixels`);
assert.deepEqual(after.before, resized.after);
assert.deepEqual(after.after, after.before);
const elapsed = name => Number(/^ElapsedMs=([0-9]+)$/m.exec(text(name))?.[1]);
const dispatchStartMs = elapsed('resize-dispatch-start.txt');
const dispatchFinishMs = elapsed('resize-dispatch-finish.txt');
assert(dispatchStartMs >= baselineMs + 3000);
assert(dispatchFinishMs > dispatchStartMs && dispatchFinishMs < 108000);
const closeSamples = text('close-attempt.txt');
const closeMs = Number(/ElapsedMs=([0-9]+);MainWindowHandle=[1-9][0-9]*;CloseMainWindowPosted=True/
  .exec(closeSamples)?.[1]);
assert(closeMs >= 108000 && closeMs < 115000 && closeMs > dispatchFinishMs);

const review = {
  schema: 'soui.win7-v02-native-resize-v11-review/v1', attempt: attemptName,
  sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: expectedFreezeSha, serialSha256: sha(serial),
  serialBytes: serial.length, serialFrames: files.size,
  qemuPid: launch.pid, gateSamples: gates.length,
  guestIdentityVerified: true, displayMode1024x768Verified: true,
  payloadPrePostVerified: true, applicationPid: pid, applicationHwnd: initial.hwnd,
  applicationNativeExitCode: 0, closeWasHarnessPosted: true,
  baselineElapsedMs: baselineMs, dispatchStartElapsedMs: dispatchStartMs,
  dispatchFinishElapsedMs: dispatchFinishMs, closeElapsedMs: closeMs,
  initialBounds: initial.before, resizeBefore: resized.before,
  resizeAfter: resized.after, postResizeBounds: after.after,
  nativeResizeVerified: true, screenshotReviewed: false,
  realScrollVerified: false, imeCompositionVerified: false,
  taskParallelVerified: false, win7CurrentV02Verified: false,
  benchmarkValid: false, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'review-v11.json'),
  JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
