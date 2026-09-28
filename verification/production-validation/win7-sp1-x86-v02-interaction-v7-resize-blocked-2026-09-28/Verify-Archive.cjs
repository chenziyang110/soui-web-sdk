'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = relative => fs.readFileSync(path.join(root, relative));
const parse = relative => JSON.parse(read(relative).toString('utf8').replace(/^\uFEFF/, ''));
const archive = parse('raw-manifest.json');
assert.equal(archive.schema, 'soui.win7-v02-v7-failure-archive/v1');
assert.equal(archive.sourceBuildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(archive.freezeSha256,
  'bec54c2130a58d2036bb4d847c6c95464f43284ed4269f3c9e7000eca675e785');
assert.equal(archive.guestResult, 'failed-before-resize');
assert.equal(archive.win7CurrentV02Verified, false);
assert.equal(archive.releaseReady, false);
assert.equal(archive.files.length, 42);
assert.equal(parse('raw-manifest-attempt1.json').files.length, 0);

const listed = new Set();
for (const item of archive.files) {
  assert.match(item.path, /^raw\/[A-Za-z0-9._/-]+$/);
  assert(!item.path.split('/').includes('..'));
  assert(!listed.has(item.path));
  listed.add(item.path);
  const bytes = read(item.path);
  assert.equal(bytes.length, item.bytes, item.path);
  assert.equal(sha(bytes), item.sha256, item.path);
}
function walk(directory, prefix) {
  return fs.readdirSync(directory, {withFileTypes: true}).flatMap(entry => {
    const relative = `${prefix}/${entry.name}`;
    assert.equal(entry.isSymbolicLink(), false);
    if (entry.isDirectory()) return walk(path.join(directory, entry.name), relative);
    assert.equal(entry.isFile(), true);
    return [relative];
  });
}
assert.deepEqual(walk(path.join(root, 'raw'), 'raw').sort(), [...listed].sort());

const inputBase = 'raw/layout-interaction-v7-attempt2/';
const input = parse(`${inputBase}input.json`);
const freezeBytes = read(`${inputBase}freeze.json`);
const freeze = parse(`${inputBase}freeze.json`);
assert.equal(sha(freezeBytes), archive.freezeSha256);
assert.equal(freeze.inputSha256, sha(read(`${inputBase}input.json`)));
assert.equal(input.sourceSha256,
  '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(input.buildId, archive.sourceBuildId);
assert.equal(input.expectedSerialFrames, 116);
assert.equal(read('raw/v7-preparation-attempt1/prepare.exitcode.txt').toString().trim(), '1');
assert.equal(read('raw/v7-preparation-attempt2/prepare.exitcode.txt').toString().trim(), '0');
assert.equal(read('raw/v7-preparation-attempt2/freeze.exitcode.txt').toString().trim(), '0');
assert.equal(read('raw/v7-preparation-attempt2/verify.exitcode.txt').toString().trim(), '0');
assert.equal(read('raw/v7-preparation-attempt2/guest-driver.exitcode.txt').toString().trim(), '0');
assert.equal(read('raw/v7-review-attempt2/review.exitcode.txt').toString().trim(), '1');

const guestBase = 'raw/guest-layout-interaction-v7-attempt2/';
const launch = parse(`${guestBase}launch.json`);
const command = parse(`${guestBase}command-send.json`);
const result = parse(`${guestBase}qemu-result.json`);
assert.equal(launch.freezeSha256, archive.freezeSha256);
assert.equal(command.pid, launch.pid);
assert.equal(command.command, input.guestCommand);
assert.equal(result.pid, launch.pid);
assert.equal(result.exitCode, 0);
assert.equal(result.externalLoadObserved, false);
assert.equal(result.diskUnchanged, true);
assert.equal(result.frozenCheckExitCode, 0);
const serialBytes = read(`${guestBase}serial-export.txt`);
assert.equal(sha(serialBytes), archive.serialSha256);
const lines = serialBytes.toString('ascii').split(/\r?\n/);
assert.equal(lines.shift(), 'SOUI_SERIAL_V1');
assert.equal(lines.pop(), '');
assert.equal(lines.pop(), 'SOUI_SERIAL_END');
assert.equal(lines.length % 2, 0);
const frames = new Map();
for (let index = 0; index < lines.length; index += 2) {
  const header = /^FILE\|([a-z0-9][a-z0-9._-]*)\|([0-9]+)\|([A-F0-9]{64})$/.exec(lines[index]);
  assert(header, `Invalid frame at ${index}`);
  assert(!frames.has(header[1]));
  const bytes = Buffer.from(lines[index + 1], 'base64');
  assert.equal(bytes.toString('base64'), lines[index + 1]);
  assert.equal(bytes.length, Number(header[2]));
  assert.equal(sha(bytes).toUpperCase(), header[3]);
  frames.set(header[1], bytes);
}
assert.equal(frames.size, 51);
const frameText = name => {
  const bytes = frames.get(name);
  assert(bytes, `Missing ${name}`);
  return bytes[0] === 0xff && bytes[1] === 0xfe
    ? bytes.subarray(2).toString('utf16le') : bytes.toString('utf8').replace(/^\uFEFF/, '');
};
assert.match(frameText('guest-identity.txt'), /Version=6\.1\.7601/);
assert.match(frameText('guest-identity.txt'), /OSArchitecture=32-bit/);
assert.equal(frameText('run-exitcode.txt').trim(), '1');
assert.match(frameText('harness-exception.txt'), /Input resize failed or timed out: 1/);
const resize = frameText('action-resize-process.txt');
assert.match(resize, /ExitCode=1/);
const stderr = /StderrBase64=([^\r\n]+)/.exec(resize);
assert(stderr);
assert.match(Buffer.from(stderr[1], 'base64').toString('utf8'),
  /resize border is covered by another window/);
for (let index = 1; index <= 12; ++index) {
  const action = JSON.parse(frameText(`action-outer-down-${index}.json`));
  assert.equal(action.action, 'wheel');
  assert.equal(action.foregroundBefore, true);
}
assert.equal(frames.has('action-start-task.json'), false);
assert.equal(frames.has('action-pinyin.json'), false);
assert.equal(frames.has('run-complete.txt'), false);
console.log(JSON.stringify({status: 'verified-failed-run', archivedFiles: archive.files.length,
  serialFrames: frames.size, outerWheelActions: 12,
  win7CurrentV02Verified: false, releaseReady: false}));
