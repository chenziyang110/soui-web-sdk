'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const attempt = path.join(root, 'guest-preflight-attempt1');
const frozen = path.join(root, 'attempt1');
const decoded = path.join(attempt, 'decoded');
assert.equal(fs.existsSync(decoded), false, 'Refuse to replace serial decode');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const launch = readJson(path.join(attempt, 'launch.json'));
const command = readJson(path.join(attempt, 'command-send.json'));
const result = readJson(path.join(attempt, 'qemu-result.json'));
const freezeBytes = fs.readFileSync(path.join(frozen, 'freeze.json'));
const input = readJson(path.join(frozen, 'preflight-input.json'));
assert.equal(sha(freezeBytes), 'b5137aa787b1885e2d11f7152292de15421ec8f0dfe1da5b72240041b794b4f0');
assert.equal(launch.freezeSha256, sha(freezeBytes));
assert.equal(launch.pid, command.pid);
assert.equal(command.command, input.guestCommand);
assert.equal(fs.readFileSync(path.join(attempt, 'monitor-script.cjs')).length > 0, true);
assert.equal(launch.monitorScriptSha256,
  sha(fs.readFileSync(path.join(attempt, 'monitor-script.cjs'))));
assert(launch.arguments.includes('-snapshot'));
assert(launch.arguments.includes('-net'));
assert(launch.arguments.includes('none'));
assert(launch.arguments.some(item => item.startsWith('file=fat:ro:')));
assert.equal(result.pid, launch.pid);
assert.equal(result.exitCode, 0);
assert.equal(result.timedOut, false);
assert.equal(result.externalLoadObserved, false);
assert.equal(result.gateFailure, null);
assert.equal(result.diskUnchanged, true);
assert.equal(result.frozenCheckExitCode, 0);
assert.equal(result.diskSha256Before, result.diskSha256After);
assert.equal(result.diskSha256Before,
  'fdf5eb826c9868622e218d1d54f328970ad190f848cd0777174885b1d072ddb4');
assert(new Date(launch.startedAt).getTime() <= new Date(command.sentAt).getTime());
assert(new Date(command.sentAt).getTime() <= new Date(result.finishedAt).getTime());

const serial = fs.readFileSync(path.join(attempt, 'serial-export.txt'));
const text = serial.toString('ascii');
assert.equal(Buffer.from(text, 'ascii').equals(serial), true, 'Serial must be ASCII frames');
const lines = text.split(/\r?\n/);
assert.equal(lines.shift(), 'SOUI_SERIAL_V1');
assert.equal(lines.pop(), '');
assert.equal(lines.pop(), 'SOUI_SERIAL_END');
assert.equal(lines.length % 2, 0, 'Every file needs header and payload');
const names = new Set();
const files = new Map();
fs.mkdirSync(decoded);
for (let i = 0; i < lines.length; i += 2) {
  const header = /^FILE\|([a-z0-9][a-z0-9._-]*)\|([0-9]+)\|([A-F0-9]{64})$/.exec(lines[i]);
  assert(header, `Invalid frame at ${i}`);
  const name = header[1];
  assert.equal(names.has(name), false, `Duplicate frame: ${name}`);
  names.add(name);
  const encoded = lines[i + 1];
  assert.match(encoded, /^[A-Za-z0-9+/]*={0,2}$/);
  const bytes = Buffer.from(encoded, 'base64');
  assert.equal(bytes.toString('base64'), encoded, `Noncanonical base64: ${name}`);
  assert.equal(bytes.length, Number(header[2]), `Frame length drift: ${name}`);
  assert.equal(sha(bytes).toUpperCase(), header[3], `Frame SHA drift: ${name}`);
  fs.writeFileSync(path.join(decoded, name), bytes, {flag: 'wx'});
  files.set(name, bytes);
}
const expected = ['display.txt', 'guest-identity.txt', 'input-layouts.txt',
  'preflight-exitcode.txt', 'preflight-result.txt', 'probe-loader-exitcode.txt',
  'probe-loader-stderr.log', 'probe-loader-stdout.log', 'transfer-observed.tsv'];
assert.deepEqual([...files.keys()].sort(), expected.sort());
const asText = name => files.get(name).toString('utf8');
const identity = asText('guest-identity.txt');
for (const pattern of [/Version=6\.1\.7601/, /ServicePackMajorVersion=1/,
  /PointerBytes=4/, /PowerShellMajor=2/]) assert.match(identity, pattern);
assert.equal(asText('preflight-exitcode.txt').trim(), '0');
assert.equal(asText('probe-loader-exitcode.txt').trim(), '1');
assert.match(asText('probe-loader-stderr.log'), /^usage: V02InputProbe /);
assert.match(asText('preflight-result.txt'), /^status=guest-identity-transfer-and-probe-loader-verified;/);
const observed = asText('transfer-observed.tsv').trim().split(/\r?\n/).map(line => {
  const parts = line.split('\t');
  assert.equal(parts.length, 3);
  return {sha256: parts[0], bytes: Number(parts[1]), path: parts[2]};
});
assert.deepEqual(observed.sort((a, b) => a.path.localeCompare(b.path)),
  input.transferFiles.map(item => ({sha256: item.sha256, bytes: item.bytes, path: item.path}))
    .sort((a, b) => a.path.localeCompare(b.path)));
const review = {
  schema: 'soui.win7-v02-current-preflight-review/v1',
  freezeSha256: sha(freezeBytes), serialSha256: sha(serial),
  serialBytes: serial.length, frameCount: files.size,
  guestIdentity: identity.trim(), display: asText('display.txt').trim(),
  inputLayouts: asText('input-layouts.txt').trim(),
  probeLoaderReachedArgumentValidation: true,
  transferMatchedFrozenInput: true, guestPreflightVerified: true,
  applicationLaunched: false, imeCompositionVerified: false,
  win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'preflight-review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
