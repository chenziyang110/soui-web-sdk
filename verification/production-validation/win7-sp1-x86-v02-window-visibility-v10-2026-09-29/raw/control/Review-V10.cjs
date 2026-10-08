'use strict';

// Read-only review of one immutable Win7 visibility diagnostic attempt.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const attempt = path.join(root, 'guest-window-visibility-v10-attempt1');
const transfer = path.join(root, 'win7-v10-transfer-attempt1');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = (base, name) => fs.readFileSync(path.join(base, name));
const json = (base, name) => {
  const bytes = read(base, name);
  const value = bytes[0] === 0xff && bytes[1] === 0xfe
    ? bytes.subarray(2).toString('utf16le')
    : bytes.toString('utf8').replace(/^\uFEFF/, '');
  return JSON.parse(value);
};
const freezeBytes = read(transfer, 'freeze.json');
const freezeSha = '7296492cb73b56822a630273ee7f9bd6b38c458f8e34e9b3fd2ed6c1aecb552c';
assert.equal(sha(freezeBytes), freezeSha);
const input = json(transfer, 'input.json');
const launch = json(attempt, 'launch.json');
const sent = json(attempt, 'command-send.json');
const result = json(attempt, 'qemu-result.json');
assert.equal(input.schema, 'soui.win7-v02-window-visibility-v10-input/v1');
assert.equal(input.sourceSha256, '60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640');
assert.equal(input.buildId, 'b6caec6c-117e-49eb-86fa-4182e64d5b55');
assert.equal(launch.inputName, 'win7-v10-transfer-attempt1');
assert.equal(launch.freezeSha256, freezeSha);
assert.equal(launch.pid, sent.pid);
assert.equal(launch.pid, result.pid);
assert.equal(sent.command, input.guestCommand);
assert.equal(fs.readdirSync(attempt).filter(name => /^monitor-run-.*\.log$/.test(name)).length, 1);
assert.equal(result.exitCode, 0);
assert.equal(result.timedOut, false);
assert.equal(result.externalLoadObserved, false);
assert.equal(result.gateFailure, null);
assert.equal(result.diskUnchanged, true);
assert.equal(result.diskSha256Before, 'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');
assert.equal(result.diskSha256After, result.diskSha256Before);
assert.equal(result.frozenCheckExitCode, 0);
for (const name of ['frozen-check-before.log', 'frozen-check-after.log']) {
  const check = json(attempt, name);
  assert.equal(check.status, 'verified');
  assert.equal(check.freezeSha256, freezeSha);
  assert.equal(check.releaseReady, false);
}
const observed = json(attempt, 'gates-during.json');
const gates = Array.isArray(observed) ? observed : [observed];
assert(gates.length >= 2);
assert(gates.every(gate => !gate.error && (!gate.processes || gate.processes.length === 0)));

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
assert.equal(files.size, 46);
const text = name => {
  assert(files.has(name), `Missing ${name}`);
  const bytes = files.get(name);
  return bytes[0] === 0xff && bytes[1] === 0xfe
    ? bytes.subarray(2).toString('utf16le') : bytes.toString('utf8').replace(/^\uFEFF/, '');
};
for (const pattern of [/Version=6\.1\.7601/, /ServicePackMajorVersion=1/,
  /OSArchitecture=32-bit/, /PointerBytes=4/, /PowerShellMajor=2/])
  assert.match(text('guest-identity.txt'), pattern);
assert.equal(text('display-mode-exitcode.txt').trim(), '0');
assert.match(text('display-mode.log'), /after width=1024 height=768 .*metrics=1024x768/);
for (const phase of ['pre', 'post']) {
  assert.equal(text(`payload-${phase}-exitcode.txt`).trim(), '0');
  assert.match(text(`payload-${phase}.log`), /SouiStarter=71,legal=5,all-hashes=matched/);
  assert.match(text(`payload-${phase}.log`), /SouiWorkbench=475,legal=5,all-hashes=matched/);
}
assert.equal(text('run-exitcode.txt').trim(), '0');
assert.equal(text('workbench-visibility-diagnostic-exitcode.txt').trim(), '0');
assert.match(text('run-complete.txt'), /status=completed;scope=1024x768-v02-window-visibility-diagnostic-no-input-not-production/);
assert.match(text('run-complete.txt'), new RegExp(`buildId=${input.buildId};source=${input.sourceSha256}`));
assert.match(text('workbench-visibility-diagnostic-run.txt'), /TimeoutMs=120000\r?\nTimedOut=False\r?\nRemainingProcess=False\r?\nExitCode=0/);
assert.match(text('close-attempt.txt'), /ElapsedMs=108[0-9]+;MainWindowHandle=[1-9][0-9]*;CloseMainWindowPosted=True/);
assert.match(text('workbench-visibility-diagnostic-smoke.log'), /window-constructed\r?\nhost-started/);
assert.match(text('harness.log'), /workbench-visibility-diagnostic exit=0/);
assert.equal([...files.keys()].filter(name => name.startsWith('action-')).length, 0);

function decodeBase64(value) {
  const bytes = Buffer.from(value, 'base64');
  assert.equal(bytes.toString('base64'), value);
  return bytes.toString('utf8');
}
const samples = text('window-inspect-samples.txt').trim().split(/\r?\n/).map(line => {
  const fields = Object.fromEntries(line.split(';').map(part => {
    const at = part.indexOf('=');
    assert(at > 0);
    return [part.slice(0, at), part.slice(at + 1)];
  }));
  return {elapsedMs: Number(fields.ElapsedMs), probeAtMs: Number(fields.ProbeAtMs),
    hwnd: Number(fields.MainWindowHandle), code: Number(fields.ExitCode),
    timedOut: fields.TimedOut, smoke: decodeBase64(fields.SmokeBase64),
    stdout: decodeBase64(fields.StdoutBase64), stderr: decodeBase64(fields.StderrBase64)};
});
assert.equal(samples.length, 20);
for (let index = 0; index < samples.length; ++index) {
  const sample = samples[index];
  assert.equal(sample.probeAtMs, 10000 + index * 5000);
  assert(sample.elapsedMs >= sample.probeAtMs && sample.elapsedMs < sample.probeAtMs + 5000);
  assert.equal(sample.timedOut, 'False');
  assert.match(sample.smoke, /window-constructed/);
  if (index < 7) {
    assert.equal(sample.code, 1);
    assert.equal(sample.hwnd, 0);
    assert.equal(sample.stdout, '');
    assert.equal(sample.stderr.trim(), 'visible unowned top-level target window missing');
    assert(!sample.smoke.includes('host-started'));
  } else {
    assert.equal(sample.code, 0);
    assert.equal(sample.hwnd, 262436);
    assert.equal(sample.stderr, '');
    assert.match(sample.smoke, /host-started/);
    const probe = JSON.parse(sample.stdout);
    assert.equal(probe.schema, 'soui.v02.os-input/v1');
    assert.equal(probe.pid, 1684);
    assert.equal(probe.hwnd, sample.hwnd);
    assert.equal(probe.action, 'inspect');
    assert.deepEqual(probe.before, [87, 24, 937, 664]);
    assert.deepEqual(probe.after, probe.before);
    assert.equal(probe.imeCompositionVerified, false);
    assert.equal(probe.releaseReady, false);
    assert.deepEqual(JSON.parse(text(`window-inspect-${sample.probeAtMs}.json`)), probe);
  }
}

const review = {
  schema: 'soui.win7-v02-window-visibility-v10-review/v1',
  attempt: 'guest-window-visibility-v10-attempt1', sourceSha256: input.sourceSha256,
  buildId: input.buildId, freezeSha256: freezeSha, serialSha256: sha(serial),
  serialBytes: serial.length, serialFrames: files.size,
  qemuPid: launch.pid, gateSamples: gates.length,
  guestIdentityVerified: true, displayMode1024x768Verified: true,
  payloadPrePostVerified: true, guestRunExitCode: 0, workbenchExitCode: 0,
  workbenchCloseWasHarnessPosted: true, probeSamples: samples.length,
  lastNotVisibleElapsedMs: samples[6].elapsedMs,
  firstVisibleElapsedMs: samples[7].elapsedMs,
  firstVisibleProbeAtMs: samples[7].probeAtMs,
  hostStartLogFirstObservedAtProbeMs: samples[7].probeAtMs,
  visibilityObserved: true, realInputVerified: false,
  resizeVerified: false, taskParallelVerified: false, imeCompositionVerified: false,
  workAreaFixVerifiedOnWin7: false, win7CurrentV02Verified: false,
  benchmarkValid: false, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'review-v10.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
