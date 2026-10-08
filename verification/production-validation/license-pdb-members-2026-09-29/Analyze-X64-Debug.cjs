'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const read = name => fs.readFileSync(path.join(root, name));
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const capture = JSON.parse(read('x64-debug-capture.json'));
assert.equal(capture.schema, 'soui.license-x64-debug-capture/v1');
assert.equal(capture.dllSha256,
  'fe4160a70587d027504c409b8fd2bc5c87d78392811bff0c969f69c41a44d951');
assert.equal(capture.debugExit, 0);
for (const [name, expectedBytes, expectedHash] of [
  ['x64-dll-debug.stdout.txt', capture.rawStdoutBytes, capture.rawStdoutSha256],
  ['x64-dll-debug.stderr.txt', capture.rawStderrBytes, capture.rawStderrSha256],
]) {
  const bytes = read(name);
  assert.equal(bytes.length, expectedBytes, `${name} length`);
  assert.equal(hash(bytes), expectedHash, `${name} digest`);
}
assert.equal(capture.rawStderrBytes, 0);
const debug = read('x64-dll-debug.stdout.txt').toString('utf8');
assert.match(debug, /Format: COFF-x86-64/);
assert.match(debug, /Type: POGO/);
assert.equal(/Type: CodeView/.test(debug), capture.hasCodeView);
assert.equal(capture.hasCodeView, false);
const linkLog = path.join(root, '../license-link-read-traces-2026-09-28/raw/x64/link.command.1.tlog');
const linkBytes = fs.readFileSync(linkLog);
assert.equal(hash(linkBytes), capture.linkLogSha256);
const link = linkBytes.toString('utf16le');
assert.equal(/(?:^|\s)\/DEBUG(?::\S+)?(?:\s|$)/i.test(link),
  capture.linkCommandHasDebugSwitch);
assert.equal(capture.linkCommandHasDebugSwitch, false);
assert.equal(/\/PDB:\"([^\"]+)\"/i.exec(link)?.[1], capture.pdbPathFromLinkCommand);
assert.equal(capture.pdbAtLinkedOutputPathExists, false);
assert.equal(capture.licenseReviewVerified, false);
assert.equal(capture.releaseReady, false);
console.log(JSON.stringify({x64DllHasCodeView: false, linkDebugSwitch: false,
  linkedOutputPdbAbsentAtCapture: true, x64MemberEvidence: false, releaseReady: false}));
