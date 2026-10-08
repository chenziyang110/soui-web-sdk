'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const inputName = process.argv[2];
assert.match(inputName || '', /^layout-interaction-v8-attempt[0-9]+$/);
const root = path.join(__dirname, inputName);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const encode = value => JSON.stringify(value, null, 2) + '\n';
const freezeBytes = fs.readFileSync(path.join(root, 'freeze.json'));
const freeze = JSON.parse(freezeBytes);
const inputBytes = fs.readFileSync(path.join(root, 'input.json'));
const input = JSON.parse(inputBytes);
assert.equal(freeze.schema, 'soui.win7-v02-current-interaction-freeze/v8');
assert.equal(input.schema, 'soui.win7-v02-current-interaction-input/v8');
assert.equal(freeze.inputSha256, sha(inputBytes));
assert.equal(freeze.filesSha256, sha(Buffer.from(encode(freeze.files))));
assert.equal(freeze.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(freeze.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(freeze.guestExecuted, false);
assert.equal(freeze.releaseReady, false);
assert.deepEqual(input.inspectScheduleMs, [10000, 15000, 20000, 25000, 30000, 35000, 40000, 45000, 50000]);
assert.deepEqual(input.actions, {moveDrag: [-80, -80], outerDown: 12, outerWheelClient: [30, 150], resizeDrag: [80, 60],
  taskClick: [635, 600], inputRevealUp: 2, inputClick: [350, 125],
  pinyin: 'zhongwen', pinyinCommitsOnce: true,
  listRevealUp: 10, innerWheel: [450, 450], taskReturnDown: 10});
assert.equal(input.expectedSerialFrames, 118);
assert.equal(input.mode, 'exploratory-visible-task-ime-real-move-scroll-resize-not-full-production');
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
assert.match(input.probeBuildName, /^probe-v8-build-attempt[0-9]+$/);
const buildBytes = fs.readFileSync(path.join(__dirname, input.probeBuildName, 'result.json'));
assert.equal(sha(buildBytes), input.probeBuildResultSha256);
assert.equal(input.probeSourceSha256,
  '6e7d8b00f0819074dbe800c2dffcbee81ac425e1c27414c5539e75ff11e98d65');
const build = JSON.parse(buildBytes);
assert.equal(build.status, 'built-x86');
assert.equal(build.schema, 'soui.v02.probe-build/v8');
assert.equal(build.executables.length, 1);
assert.equal(build.executables[0].sha256, input.probeSha256);
assert.match(fs.readFileSync(path.join(transfer, 'export-serial.ps1'), 'utf8'), /if\(\$files\.Count -gt 128\)/);
const guest = fs.readFileSync(path.join(transfer, 'run-display-explore.ps1'), 'utf8');
for (const marker of [
  'interaction-baseline-ready.txt', 'interaction-dispatch-start.txt',
  'interaction-dispatch-finish.txt', '$wheelIndex -le 12',
  "'move-drag' '-80,-80' 'move'",
  "'wheel' '30,150' ('outer-down-'",
  "'click' '635,600' 'start-task'",
  "'click' '350,125' 'focus-input'",
  "'pinyin' 'zhongwen' 'pinyin'",
  "'wheel-up' '30,150' ('input-reveal-'",
  "'wheel-up' '30,150' ('list-reveal-'",
  "'wheel' '450,450' 'inner-list-wheel'",
  "'wheel' '30,150' ('task-return-'",
  "'resize-drag' '80,60' 'resize'",
  'scope=1024x768-v02-visible-task-ime-real-move-scroll-resize-exploration-not-production',
]) assert(guest.includes(marker), 'Missing guest script marker: ' + marker);
assert(guest.indexOf("'move-drag' '-80,-80' 'move'") <
  guest.indexOf("'resize-drag' '80,60' 'resize'"));
assert(guest.indexOf("'resize-drag' '80,60' 'resize'") <
  guest.indexOf("'click' '635,600' 'start-task'"));
assert(guest.indexOf("'click' '635,600' 'start-task'") <
  guest.indexOf("'click' '350,125' 'focus-input'"));
assert.doesNotMatch(guest, /'key-space'/);
console.log(JSON.stringify({status: 'verified', files: freeze.files.length,
  freezeSha256: sha(freezeBytes), win7CurrentV02Verified: false, releaseReady: false}));
