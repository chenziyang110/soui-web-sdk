'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = __dirname;
const outputName = process.argv[2];
const buildName = process.argv[3];
assert.match(outputName || '', /^layout-interaction-v8-attempt[0-9]+$/);
assert.match(buildName || '', /^probe-v8-build-attempt[0-9]+$/);
const priorRoot = path.join(root, 'layout-explore-v2-attempt1');
const prior = path.join(priorRoot, 'volume', 'soui-input');
const output = path.join(root, outputName);
const transfer = path.join(output, 'volume', 'soui-input');
const source = path.join(root, 'layout-interaction-v8-source');
const probe = path.join(root, buildName, 'build', 'x86', 'Release', 'V02InputProbe.exe');
const buildResult = JSON.parse(fs.readFileSync(path.join(root, buildName, 'result.json'), 'utf8'));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
function ensureIdle() {
  const script = "$ErrorActionPreference='Stop';$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$'}|Select-Object ProcessId,Name);[ordered]@{busy=$busy}|ConvertTo-Json -Compress -Depth 3";
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', script],
    {encoding: 'utf8', windowsHide: true, timeout: 15000});
  if (result.error || result.status !== 0 || result.stderr.trim())
    throw Error(`CIM gate failed: ${result.error || result.stderr || result.status}`);
  const value = JSON.parse(result.stdout.replace(/^\uFEFF/, ''));
  const busy = Array.isArray(value.busy) ? value.busy : value.busy ? [value.busy] : [];
  if (busy.length) throw Error(`Other desktop workload active: ${JSON.stringify(busy)}`);
}
assert.equal(fs.existsSync(output), false, 'Refuse to replace interaction input');
ensureIdle();
assert.equal(sha(read(path.join(priorRoot, 'freeze.json'))),
  '3380cde0efbddb9bc18a0eb148dd074086e821ef795487df6da928719fb344ae');
assert.equal(buildResult.status, 'built-x86');
assert.equal(buildResult.schema, 'soui.v02.probe-build/v8');
assert.equal(buildResult.staticRuntime?.releaseWin32StaticRuntime, true);
assert.equal(buildResult.executables.length, 1);
assert.equal(buildResult.executables[0].architecture, 'x86');
assert.equal(sha(read(probe)), buildResult.executables[0].sha256);
assert.equal(sha(read(path.join(source, 'V02InputProbe.cpp'))),
  '6e7d8b00f0819074dbe800c2dffcbee81ac425e1c27414c5539e75ff11e98d65');
const priorInput = JSON.parse(read(path.join(priorRoot, 'input.json')));
assert.equal(priorInput.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(priorInput.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
const scriptBytes = read(path.join(source, 'run-layout-interaction.ps1'));
const scriptText = scriptBytes.toString('utf8');
const serialBytes = read(path.join(source, 'export-serial.ps1'));
assert.match(serialBytes.toString('utf8'), /if\(\$files\.Count -gt 128\)/);
for (const marker of [
  'C:\\soui-v02-current-interaction-v8-results',
  "$wheelIndex -le 12", "$wheelIndex -le 2", "$wheelIndex -le 10",
  "'move-drag' '-80,-80' 'move'",
  "'wheel' '30,150' ('outer-down-'",
  "'click' '635,600' 'start-task'",
  "'click' '350,125' 'focus-input'",
  "'pinyin' 'zhongwen' 'pinyin'",
  "'wheel-up' '30,150' ('input-reveal-'",
  "'wheel-up' '30,150' ('list-reveal-'",
  "'wheel' '30,150' ('task-return-'",
  "'resize-drag' '80,60' 'resize'",
  "'wheel' '450,450' 'inner-list-wheel'",
  '--width=850 --height=640', 'TimeoutMs=120000',
  'scope=1024x768-v02-visible-task-ime-real-move-scroll-resize-exploration-not-production',
]) assert(scriptText.includes(marker), 'Missing frozen v8 action marker: ' + marker);
assert(scriptText.indexOf("'move-drag' '-80,-80' 'move'") <
  scriptText.indexOf("'resize-drag' '80,60' 'resize'"));
assert(scriptText.indexOf("'resize-drag' '80,60' 'resize'") <
  scriptText.indexOf("'click' '635,600' 'start-task'"));
assert(scriptText.indexOf("'click' '635,600' 'start-task'") <
  scriptText.indexOf("'click' '350,125' 'focus-input'"));
assert.doesNotMatch(scriptText, /'key-space'/);

fs.mkdirSync(transfer, {recursive: true});
const replaced = new Set(['run-display-explore.ps1', 'export-serial.ps1',
  'transfer-files.tsv', 'V02InputProbe.exe']);
for (const item of fs.readdirSync(prior, {withFileTypes: true})) {
  ensureIdle();
  assert.equal(item.isFile(), true);
  if (replaced.has(item.name)) continue;
  const from = path.join(prior, item.name);
  const to = path.join(transfer, item.name);
  fs.copyFileSync(from, to, fs.constants.COPYFILE_EXCL);
  assert.equal(sha(read(from)), sha(read(to)));
}
for (const [from, to] of [
  [path.join(source, 'run-layout-interaction.ps1'), 'run-display-explore.ps1'],
  [path.join(source, 'export-serial.ps1'), 'export-serial.ps1'],
  [probe, 'V02InputProbe.exe'],
]) {
  fs.copyFileSync(from, path.join(transfer, to), fs.constants.COPYFILE_EXCL);
  assert.equal(sha(read(from)), sha(read(path.join(transfer, to))));
}
const files = fs.readdirSync(transfer, {withFileTypes: true}).map(item => {
  assert.equal(item.isFile(), true);
  assert.match(item.name, /^[A-Za-z0-9][A-Za-z0-9._-]*$/);
  const bytes = read(path.join(transfer, item.name));
  return {path: item.name, bytes: bytes.length, sha256: sha(bytes)};
}).sort((a, b) => a.path.localeCompare(b.path));
assert.equal(files.length, 11);
const manifest = Buffer.from('\uFEFF' + files.map(item =>
  `${item.sha256}\t${item.bytes}\t${item.path}`).join('\r\n') + '\r\n', 'utf8');
fs.writeFileSync(path.join(transfer, 'transfer-files.tsv'), manifest, {flag: 'wx'});
const input = {
  schema: 'soui.win7-v02-current-interaction-input/v8',
  version: priorInput.version, buildId: priorInput.buildId,
  sourceSha256: priorInput.sourceSha256,
  starterPayloadSha256: priorInput.starterPayloadSha256,
  workbenchPayloadSha256: priorInput.workbenchPayloadSha256,
  probeBuildName: buildName,
  probeBuildResultSha256: sha(read(path.join(root, buildName, 'result.json'))),
  probeSourceSha256: sha(read(path.join(source, 'V02InputProbe.cpp'))),
  probeSha256: sha(read(probe)), guestScriptSha256: sha(scriptBytes),
  serialScriptSha256: sha(serialBytes), serialFrameLimit: 128,
  expectedSerialFrames: 118,
  transferManifestSha256: sha(manifest), transferFiles: files,
  mountRoot: path.join(output, 'volume'), inputSubdir: 'soui-input',
  guestCommand: priorInput.guestCommand,
  appArguments: '--arch=x86 --renderer=cpu --react --page=data --width=850 --height=640',
  requestedDisplay: '1024x768',
  inspectScheduleMs: [10000, 15000, 20000, 25000, 30000, 35000, 40000, 45000, 50000],
  actions: {moveDrag: [-80, -80], outerDown: 12, outerWheelClient: [30, 150], resizeDrag: [80, 60],
    taskClick: [635, 600], inputRevealUp: 2, inputClick: [350, 125],
    pinyin: 'zhongwen', pinyinCommitsOnce: true,
    listRevealUp: 10, innerWheel: [450, 450], taskReturnDown: 10},
  closeScheduleMs: [90000, 95000, 100000, 105000, 110000],
  appDeadlineMs: 120000,
  mode: 'exploratory-visible-task-ime-real-move-scroll-resize-not-full-production',
  guestExecuted: false, win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(output, 'input.json'), JSON.stringify(input, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({status: 'prepared', transferFiles: files.length,
  transferManifestSha256: sha(manifest), probeSha256: input.probeSha256,
  guestScriptSha256: input.guestScriptSha256, serialScriptSha256: input.serialScriptSha256,
  expectedSerialFrames: input.expectedSerialFrames, guestExecuted: false, releaseReady: false}));
