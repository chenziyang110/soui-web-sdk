'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const attempt = path.join(root, 'guest-app-smoke-attempt1');
const frozen = path.join(root, 'app-smoke-attempt1');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const bytes = name => fs.readFileSync(path.join(attempt, name));
const json = name => JSON.parse(bytes(name).toString('utf8').replace(/^\uFEFF/, ''));
const freezeBytes = fs.readFileSync(path.join(frozen, 'freeze.json'));
const input = JSON.parse(fs.readFileSync(path.join(frozen, 'input.json'), 'utf8'));
assert.equal(sha(freezeBytes), '6b11fc16ae74e00e528d14ab7aa301b49034348b8f8446a192c32b28c57445a3');
const launch = json('launch.json'), command = json('command-send.json'), result = json('qemu-result.json');
assert.equal(launch.freezeSha256, sha(freezeBytes));
assert.equal(launch.pid, command.pid);
assert.equal(command.command, input.guestCommand);
assert.equal(launch.monitorScriptSha256, sha(bytes('monitor-script.cjs')));
assert(launch.arguments.includes('-snapshot'));
assert(launch.arguments.includes('-net') && launch.arguments.includes('none'));
assert(launch.arguments.some(x => x.startsWith('file=fat:ro:')));
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
const serial = bytes('serial-export.txt');
const text = serial.toString('ascii');
assert(Buffer.from(text, 'ascii').equals(serial));
const lines = text.split(/\r?\n/);
assert.equal(lines.shift(), 'SOUI_SERIAL_V1');
assert.equal(lines.pop(), '');
assert.equal(lines.pop(), 'SOUI_SERIAL_END');
assert.equal(lines.length % 2, 0);
const files = new Map();
for (let i = 0; i < lines.length; i += 2) {
  const header = /^FILE\|([a-z0-9][a-z0-9._-]*)\|([0-9]+)\|([A-F0-9]{64})$/.exec(lines[i]);
  assert(header, `Invalid frame ${i}`);
  assert.equal(files.has(header[1]), false);
  const encoded = lines[i + 1];
  assert.match(encoded, /^[A-Za-z0-9+/]*={0,2}$/);
  const decoded = Buffer.from(encoded, 'base64');
  assert.equal(decoded.toString('base64'), encoded);
  assert.equal(decoded.length, Number(header[2]));
  assert.equal(sha(decoded).toUpperCase(), header[3]);
  assert(sha(decoded) === sha(fs.readFileSync(path.join(attempt, 'decoded', header[1]))));
  files.set(header[1], decoded);
}
const expected = ['guest-identity.txt', 'harness.log', 'payload-post.log', 'payload-post-exitcode.txt',
  'payload-pre.log', 'payload-pre-exitcode.txt', 'run-complete.txt', 'run-exitcode.txt',
  'souistarter-payload-post.tsv', 'souistarter-payload-pre.tsv',
  'souiworkbench-payload-post.tsv', 'souiworkbench-payload-pre.tsv',
  'starter-extract.log', 'starter-extract-exitcode.txt',
  'starter-zip-test.log', 'starter-zip-test-exitcode.txt',
  'transfer-diagnostics.txt', 'transfer-entries.tsv', 'transfer-lines.tsv',
  'transfer-manifest-raw.tsv', 'transfer-observed.tsv', 'workbench-current-exitcode.txt',
  'workbench-current-navigation.log', 'workbench-current-run.txt', 'workbench-current-smoke.log',
  'workbench-extract.log', 'workbench-extract-exitcode.txt',
  'workbench-zip-test.log', 'workbench-zip-test-exitcode.txt'];
assert.deepEqual([...files.keys()].sort(), expected.sort());
const asText = name => files.get(name).toString('utf8').replace(/^\uFEFF/, '');
const identity = asText('guest-identity.txt');
for (const pattern of [/Version=6\.1\.7601/, /ServicePackMajorVersion=1/,
  /OSArchitecture=32-bit/, /PointerBytes=4/, /PowerShellMajor=2/]) assert.match(identity, pattern);
for (const name of ['run-exitcode.txt', 'workbench-current-exitcode.txt',
  'payload-pre-exitcode.txt', 'payload-post-exitcode.txt',
  'starter-zip-test-exitcode.txt', 'starter-extract-exitcode.txt',
  'workbench-zip-test-exitcode.txt', 'workbench-extract-exitcode.txt'])
  assert.equal(asText(name).trim(), '0', name);
assert.match(asText('workbench-current-run.txt'), /Arguments=--webview-smoke-test --arch=x86 --renderer=cpu --react --page=data --width=700 --height=500/);
assert.match(asText('workbench-current-run.txt'), /TimeoutMs=60000\r?\nTimedOut=False\r?\nRemainingProcess=False\r?\nExitCode=0/);
const smoke = asText('workbench-current-smoke.log');
let previous = -1;
for (const token of ['process-start', 'window-constructed', 'host-started',
  'timer-document-ready', 'application-ready', 'host-close-start', 'adapter-unmount-start',
  'adapter-bridge-shutdown', 'adapter-events-drained', 'adapter-webview-destroyed',
  'host-close-complete', 'window-destroyed']) {
  const at = smoke.indexOf(token, previous + 1);
  assert(at > previous, `Missing/out-of-order smoke phase ${token}`);
  previous = at;
}
assert.match(smoke, /document=ready,bridge=ready/);
assert.match(asText('workbench-current-navigation.log'), /allowed=1 url=file:\/\/\/C:\/soui-v02-current-app-smoke\/SouiWorkbench\/resources\/react\.html/);
assert.match(asText('run-complete.txt'), /buildId=f0c38056-058a-456e-be99-17446127f7ec/);
assert.match(asText('run-complete.txt'), /source=15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d;apps=1;status=completed;scope=short-smoke-not-v02-input/);
assert.equal(sha(files.get('transfer-manifest-raw.tsv')), input.transferManifestSha256);
const observedTransfer = asText('transfer-observed.tsv').trim().split(/\r?\n/).map(line => {
  const fields = line.split('\t'); assert.equal(fields.length, 3);
  return {sha256: fields[0], bytes: Number(fields[1]), path: fields[2]};
});
const manifest = fs.readFileSync(path.join(frozen, 'volume/soui-input/transfer-files.tsv'));
assert.deepEqual(observedTransfer.sort((a, b) => a.path.localeCompare(b.path)),
  [...input.transferFiles, {sha256: sha(manifest), bytes: manifest.length, path: 'transfer-files.tsv'}]
    .sort((a, b) => a.path.localeCompare(b.path)));
for (const app of ['SouiStarter', 'SouiWorkbench']) {
  const manifestLines = fs.readFileSync(path.join(frozen, 'volume/soui-input', `${app}-payload.tsv`), 'utf8')
    .replace(/^\uFEFF/, '').trim().split(/\r?\n/);
  assert.equal(manifestLines.length, app === 'SouiStarter' ? 71 : 475);
  const legal = manifestLines.filter(line => line.split('\t')[2].startsWith('resources/legal/'));
  assert.equal(legal.length, 5);
  for (const phase of ['pre', 'post']) {
    const observed = asText(`${app.toLowerCase()}-payload-${phase}.tsv`).trim().split(/\r?\n/);
    assert.deepEqual(observed, manifestLines, `${app} ${phase} payload drift`);
  }
}
const gates = json('gates-during.json');
const samples = Array.isArray(gates) ? gates : [gates];
assert(samples.length > 0);
assert(samples.every(item => !item.error && (!item.processes || item.processes.length === 0)));
const review = {
  schema: 'soui.win7-v02-current-app-smoke-review/v1',
  sourceSha256: input.sourceSha256, buildId: input.buildId,
  freezeSha256: sha(freezeBytes), serialSha256: sha(serial), serialBytes: serial.length,
  serialFrames: files.size, qemuPid: launch.pid, gateSamples: samples.length,
  guestIdentity: identity.trim(), applicationNativeExitCode: 0,
  applicationReady: true, bridgeReady: true, fullClose: true,
  completePayloadsAndLegalPrePost: true, currentWorkbenchShortSmokeVerified: true,
  dataPageVisibleVerified: false, v02RealInputVerified: false,
  win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'smoke-review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
