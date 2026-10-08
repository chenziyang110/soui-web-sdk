'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, 'layout-interaction-v3-attempt1');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const encode = value => JSON.stringify(value, null, 2) + '\n';
const freezeBytes = fs.readFileSync(path.join(root, 'freeze.json'));
const freeze = JSON.parse(freezeBytes);
const inputBytes = fs.readFileSync(path.join(root, 'input.json'));
const input = JSON.parse(inputBytes);
assert.equal(freeze.schema, 'soui.win7-v02-current-interaction-freeze/v3');
assert.equal(input.schema, 'soui.win7-v02-current-interaction-input/v3');
assert.equal(freeze.inputSha256, sha(inputBytes));
assert.equal(freeze.filesSha256, sha(Buffer.from(encode(freeze.files))));
assert.equal(freeze.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(freeze.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(freeze.guestExecuted, false);
assert.equal(freeze.releaseReady, false);
assert.deepEqual(input.inspectScheduleMs, [10000, 15000, 20000, 25000, 30000, 35000, 40000, 45000, 50000]);
assert.deepEqual(input.actions, {outerDown: 12, taskClick: [600, 560], outerUp: 4,
  inputClick: [350, 350], pinyin: 'zhongwen', commitSpace: true,
  resizeDrag: [80, 60], returnUp: 8, innerWheel: [450, 450]});
assert.equal(input.expectedSerialFrames, 98);
assert.equal(input.mode, 'exploratory-native-ime-resize-task-not-full-production');
assert.deepEqual(input.closeScheduleMs, [90000, 95000, 100000, 105000, 110000]);
assert.equal(input.appDeadlineMs, 120000);
assert.equal(input.serialFrameLimit, 128);
assert.equal(input.appArguments, '--arch=x86 --renderer=cpu --react --page=data --width=850 --height=640');
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
assert.equal(sha(fs.readFileSync(path.join(transfer, 'export-serial.ps1'))), input.serialScriptSha256);
assert.equal(sha(fs.readFileSync(path.join(transfer, 'V02InputProbe.exe'))), input.probeSha256);
assert.equal(input.probeSha256, '929ecfcb2d842b1639b31a349ca83bd2f037d2111600526e1aa9cdb0001ab26e');
assert.match(fs.readFileSync(path.join(transfer, 'export-serial.ps1'), 'utf8'), /if\(\$files\.Count -gt 128\)/);
const guest = fs.readFileSync(path.join(transfer, 'run-display-explore.ps1'), 'utf8');
for (const marker of [/interaction-baseline-ready\.txt/, /interaction-dispatch-start\.txt/,
  /interaction-dispatch-finish\.txt/, /\$wheelIndex -le 12/,
  /'click' '600,560' 'start-task'/, /'click' '350,350' 'focus-input'/,
  /'pinyin' 'zhongwen' 'pinyin'/, /'resize-drag' '80,60' 'resize'/,
  /scope=1024x768-v02-real-ime-resize-task-exploration-not-production/])
  assert.match(guest, marker);
console.log(JSON.stringify({status: 'verified', files: freeze.files.length,
  freezeSha256: sha(freezeBytes), win7CurrentV02Verified: false, releaseReady: false}));
