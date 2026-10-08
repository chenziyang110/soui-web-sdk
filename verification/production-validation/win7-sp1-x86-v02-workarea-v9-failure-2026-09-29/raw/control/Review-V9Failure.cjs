'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const attempt = path.join(root, 'guest-work-area-v9-attempt1');
const frozen = path.join(root, 'win7-v9-transfer-attempt1');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = name => fs.readFileSync(path.join(attempt, name));
const json = name => JSON.parse(read(name).toString('utf8').replace(/^\uFEFF/, ''));
const freeze = fs.readFileSync(path.join(frozen, 'freeze.json'));
const input = JSON.parse(fs.readFileSync(path.join(frozen, 'input.json'), 'utf8'));
assert.equal(sha(freeze), '98acf9d01407a3c4bcbe843b6d9289598d607f3c32e20485759e6fc4b1dc6e80');
assert.equal(input.sourceSha256, '60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640');
assert.equal(input.buildId, 'b6caec6c-117e-49eb-86fa-4182e64d5b55');

const launch = json('launch.json');
const sent = json('command-send.json');
const result = json('qemu-result.json');
assert.equal(launch.pid, sent.pid);
assert.equal(result.pid, launch.pid);
assert.equal(launch.freezeSha256, sha(freeze));
assert.equal(sent.command, input.guestCommand);
assert.equal(result.exitCode, 0);
assert.equal(result.timedOut, false);
assert.equal(result.externalLoadObserved, false);
assert.equal(result.gateFailure, null);
assert.equal(result.diskUnchanged, true);
assert.equal(result.frozenCheckExitCode, 0);
assert.equal(result.diskSha256Before, 'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');
assert.equal(result.diskSha256After, result.diskSha256Before);
const gatesValue = json('gates-during.json');
const gates = Array.isArray(gatesValue) ? gatesValue : [gatesValue];
assert(gates.length > 0);
assert(gates.every(gate => !gate.error && (!gate.processes || gate.processes.length === 0)));

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
  const bytes = Buffer.from(lines[index + 1], 'base64');
  assert.equal(bytes.toString('base64'), lines[index + 1]);
  assert.equal(bytes.length, Number(header[2]));
  assert.equal(sha(bytes).toUpperCase(), header[3]);
  files.set(header[1], bytes);
}
const text = name => {
  assert(files.has(name), `Missing ${name}`);
  const bytes = files.get(name);
  return bytes[0] === 0xff && bytes[1] === 0xfe
    ? bytes.subarray(2).toString('utf16le') : bytes.toString('utf8').replace(/^\uFEFF/, '');
};
assert.equal(files.size, 29);
assert.equal(input.expectedSerialFrames, 116);
for (const pattern of [/Version=6\.1\.7601/, /ServicePackMajorVersion=1/,
  /OSArchitecture=32-bit/, /PointerBytes=4/, /PowerShellMajor=2/])
  assert.match(text('guest-identity.txt'), pattern);
assert.equal(text('display-mode-exitcode.txt').trim(), '0');
assert.match(text('display-mode.log'), /after width=1024 height=768 .*metrics=1024x768/);
assert.equal(text('payload-pre-exitcode.txt').trim(), '0');
assert.equal(text('run-exitcode.txt').trim(), '1');
assert.equal(text('workbench-visible-explore-exitcode.txt').trim(), '0');
assert.match(text('workbench-visible-explore-run.txt'), /TimedOut=False\r?\nRemainingProcess=False\r?\nExitCode=0/);
assert.match(text('close-attempt.txt'), /CloseMainWindowPosted=True/);
assert.match(text('harness.log'), /failed: workbench-visible-explore did not complete visible inspection, native input sequence and natural close/);
assert.match(text('workbench-visible-explore-smoke.log'), /window-constructed\r?\nhost-started/);
assert.match(text('workbench-visible-explore-navigation.log'), /allowed=1 url=file:\/\/\//);
assert.equal(files.has('window-inspect.json'), false);
assert.equal(files.has('interaction-baseline-ready.txt'), false);
assert.equal(files.has('run-complete.txt'), false);
assert.equal([...files.keys()].filter(name => name.startsWith('action-')).length, 0);
const probeSamples = text('window-inspect-samples.txt').trim().split(/\r?\n/);
assert.equal(probeSamples.length, 9);
assert(probeSamples.every(line => /ExitCode=1;TimedOut=False;StdoutBase64=;/.test(line)));
const probeError = Buffer.from(probeSamples[0].split('StderrBase64=')[1], 'base64').toString('utf8').trim();
assert.equal(probeError, 'visible unowned top-level target window missing');
assert(probeSamples.every(line => Buffer.from(line.split('StderrBase64=')[1], 'base64').toString('utf8').trim() === probeError));

const review = {
  schema: 'soui.win7-v02-workarea-v9-failure-review/v1',
  attempt: 'guest-work-area-v9-attempt1', sourceSha256: input.sourceSha256,
  buildId: input.buildId, freezeSha256: sha(freeze), serialSha256: sha(serial),
  serialBytes: serial.length, serialFrames: files.size,
  expectedSerialFrames: input.expectedSerialFrames, qemuPid: launch.pid,
  gateSamples: gates.length, guestIdentityVerified: true,
  displayMode1024x768Verified: true, payloadPreVerified: true,
  workbenchProcessExitCode: 0, workbenchCloseWasHarnessPosted: true,
  windowProbeAttempts: probeSamples.length, windowProbeError: probeError,
  windowVisibleVerified: false, resizeVerified: false,
  taskParallelVerified: false, imeCompositionVerified: false,
  workAreaFixVerifiedOnWin7: false, win7CurrentV02Verified: false,
  releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'failure-review-v9.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
