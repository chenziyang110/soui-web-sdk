'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, 'attempt1');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const freeze = JSON.parse(fs.readFileSync(path.join(root, 'freeze.json'), 'utf8'));
const inputBytes = fs.readFileSync(path.join(root, 'preflight-input.json'));
const input = JSON.parse(inputBytes);
assert.equal(freeze.inputSha256, sha(inputBytes));
assert.equal(freeze.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(freeze.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(input.guestCommand, 'cmd.exe /c D:\\soui-input\\run-preflight.cmd');
assert.equal(input.guestApplicationLaunch, false);
assert.equal(input.preflightOnly, true);
assert.equal(input.guestExecuted, false);
assert.equal(freeze.files.length, new Set(freeze.files.map(item => item.path)).size);
for (const entry of freeze.files) {
  assert(!entry.path.includes('..') && !path.isAbsolute(entry.path));
  const bytes = fs.readFileSync(path.join(root, entry.path));
  assert.equal(bytes.length, entry.bytes, `Size drift: ${entry.path}`);
  assert.equal(sha(bytes), entry.sha256, `SHA drift: ${entry.path}`);
}
function walk(relative = '') {
  return fs.readdirSync(path.join(root, relative), {withFileTypes: true}).flatMap(item => {
    assert.equal(item.isSymbolicLink(), false);
    const child = relative ? `${relative}/${item.name}` : item.name;
    if (item.isDirectory()) return walk(child);
    assert.equal(item.isFile(), true);
    return [child];
  }).sort();
}
assert.deepEqual(walk().filter(file => file !== 'freeze.json'), freeze.files.map(item => item.path).sort());
const transfer = path.join(root, 'volume/soui-input');
const manifest = fs.readFileSync(path.join(transfer, 'transfer-files.tsv'));
assert.equal(sha(manifest), input.transferManifestSha256);
assert.deepEqual(fs.readdirSync(transfer).sort(),
  [...input.transferFiles.map(item => item.path), 'transfer-files.tsv'].sort());
for (const item of input.transferFiles) {
  const bytes = fs.readFileSync(path.join(transfer, item.path));
  assert.equal(bytes.length, item.bytes);
  assert.equal(sha(bytes), item.sha256);
}
console.log(JSON.stringify({status: 'preflight-freeze-verified', files: freeze.files.length,
  freezeSha256: sha(fs.readFileSync(path.join(root, 'freeze.json'))),
  guestExecuted: false, releaseReady: false}));
