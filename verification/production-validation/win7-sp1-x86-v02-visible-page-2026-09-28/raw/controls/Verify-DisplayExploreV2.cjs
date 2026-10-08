'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, 'display-explore-v2-attempt1');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const encode = value => JSON.stringify(value, null, 2) + '\n';
const freezeBytes = fs.readFileSync(path.join(root, 'freeze.json'));
const freeze = JSON.parse(freezeBytes);
const inputBytes = fs.readFileSync(path.join(root, 'input.json'));
const input = JSON.parse(inputBytes);
assert.equal(freeze.schema, 'soui.win7-v02-current-display-explore-freeze/v2');
assert.equal(input.schema, 'soui.win7-v02-current-display-explore-input/v2');
assert.equal(freeze.inputSha256, sha(inputBytes));
assert.equal(freeze.filesSha256, sha(Buffer.from(encode(freeze.files))));
assert.equal(freeze.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(freeze.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(freeze.guestExecuted, false);
assert.equal(freeze.releaseReady, false);
assert.deepEqual(input.inspectScheduleMs, [10000, 15000, 20000, 25000, 30000, 35000, 40000]);
assert.deepEqual(input.closeScheduleMs, [45000, 50000, 55000]);
assert.equal(input.appDeadlineMs, 60000);
function walk(base, prefix = '') {
  return fs.readdirSync(base, {withFileTypes: true}).flatMap(item => {
    assert.equal(item.isSymbolicLink(), false);
    const relative = prefix ? `${prefix}/${item.name}` : item.name;
    if (item.isDirectory()) return walk(path.join(base, item.name), relative);
    assert.equal(item.isFile(), true);
    return [relative];
  }).sort();
}
assert.deepEqual(walk(root), [...freeze.files.map(item => item.path), 'freeze.json'].sort());
for (const item of freeze.files) {
  const bytes = fs.readFileSync(path.join(root, item.path));
  assert.equal(bytes.length, item.bytes, `Size drift: ${item.path}`);
  assert.equal(sha(bytes), item.sha256, `SHA drift: ${item.path}`);
}
const transfer = path.join(root, 'volume', 'soui-input');
const listed = input.transferFiles.map(item => item.path).sort();
assert.deepEqual(fs.readdirSync(transfer).sort(), [...listed, 'transfer-files.tsv'].sort());
for (const item of input.transferFiles) {
  const bytes = fs.readFileSync(path.join(transfer, item.path));
  assert.equal(bytes.length, item.bytes);
  assert.equal(sha(bytes), item.sha256);
}
assert.equal(sha(fs.readFileSync(path.join(transfer, 'transfer-files.tsv'))), input.transferManifestSha256);
assert.equal(sha(fs.readFileSync(path.join(transfer, 'run-display-explore.ps1'))), input.guestScriptSha256);
console.log(JSON.stringify({status: 'verified', files: freeze.files.length,
  freezeSha256: sha(freezeBytes), win7CurrentV02Verified: false, releaseReady: false}));
