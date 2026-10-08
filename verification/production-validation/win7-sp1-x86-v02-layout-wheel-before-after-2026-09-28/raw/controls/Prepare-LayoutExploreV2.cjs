'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = __dirname;
const priorRoot = path.join(root, 'layout-explore-v1-attempt1');
const prior = path.join(priorRoot, 'volume', 'soui-input');
const output = path.join(root, 'layout-explore-v2-attempt1');
const transfer = path.join(output, 'volume', 'soui-input');
const guestScript = path.join(root, 'layout-explore-v2-source', 'run-layout-explore.ps1');
const serialScript = path.join(root, 'layout-explore-v2-source', 'export-serial.ps1');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
function ensureIdle() {
  const script = "$ErrorActionPreference='Stop';$busy=@(Get-CimInstance Win32_Process -ErrorAction Stop|Where-Object {$_.Name -match '^(qemu-.*|Soui.*|InstalledRenewalProbe|RenewalStarter|UpgradeLedger|soui_.*tests|MSBuild|cl|link|cdb)\\.exe$'}|Select-Object ProcessId,Name);[ordered]@{busy=$busy}|ConvertTo-Json -Compress -Depth 3";
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-Command', script],
    {encoding: 'utf8', windowsHide: true, timeout: 15000});
  if (result.error || result.status !== 0 || result.stderr.trim())
    throw Error(`CIM gate failed: ${result.error || result.stderr || result.status}`);
  const parsed = JSON.parse(result.stdout.replace(/^\uFEFF/, ''));
  const busy = Array.isArray(parsed.busy) ? parsed.busy : parsed.busy ? [parsed.busy] : [];
  if (busy.length) throw Error(`Other desktop workload active: ${JSON.stringify(busy)}`);
}
assert.equal(fs.existsSync(output), false, 'Refuse to replace layout exploration input');
ensureIdle();
assert.equal(sha(read(path.join(priorRoot, 'freeze.json'))),
  'c0c5b6c54198c25397756987f39bd396d934436e33e159054c93abcb60477a20');
const priorInput = JSON.parse(read(path.join(priorRoot, 'input.json')));
const scriptBytes = read(guestScript);
const scriptText = scriptBytes.toString('utf8');
const serialBytes = read(serialScript);
assert.match(serialBytes.toString('utf8'), /if\(\$files\.Count -gt 64\)/);
for (const marker of [
  /C:\\soui-v02-current-layout-explore-v2-results/,
  /Run-Wheel \$pidValue \$creationMatch\.Groups\[1\]\.Value \$wheelIndex/,
  /wheel 900,450/,
  /TimeoutMs=120000/,
  /--width=950 --height=550/,
  /scope=1024x768-layout-wheel-exploration-v2-before-after-not-full-v02-input/,
  /wheel-baseline-ready\.txt/,
  /wheel-dispatch-start\.txt/,
  /wheel-dispatch-finish\.txt/,
  /\$wheelIndex -le 12/,
  /\[Threading\.Thread\]::Sleep\(8000\)/,
]) assert.match(scriptText, marker);

fs.mkdirSync(transfer, {recursive: true});
for (const item of fs.readdirSync(prior, {withFileTypes: true})) {
  ensureIdle();
  assert.equal(item.isFile(), true);
  if (['run-display-explore.ps1', 'export-serial.ps1', 'transfer-files.tsv'].includes(item.name)) continue;
  const source = path.join(prior, item.name);
  const destination = path.join(transfer, item.name);
  fs.copyFileSync(source, destination, fs.constants.COPYFILE_EXCL);
  assert.equal(sha(read(source)), sha(read(destination)));
}
fs.writeFileSync(path.join(transfer, 'run-display-explore.ps1'), scriptBytes, {flag: 'wx'});
fs.writeFileSync(path.join(transfer, 'export-serial.ps1'), serialBytes, {flag: 'wx'});
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
  schema: 'soui.win7-v02-current-layout-explore-input/v2',
  version: priorInput.version, buildId: priorInput.buildId,
  sourceSha256: priorInput.sourceSha256,
  starterPayloadSha256: priorInput.starterPayloadSha256,
  workbenchPayloadSha256: priorInput.workbenchPayloadSha256,
  probeSha256: priorInput.probeSha256,
  displayProbeSha256: priorInput.displayProbeSha256,
  guestScriptSha256: sha(scriptBytes),
  serialScriptSha256: sha(serialBytes), serialFrameLimit: 64,
  transferManifestSha256: sha(manifest), transferFiles: files,
  mountRoot: path.join(output, 'volume'), inputSubdir: 'soui-input',
  guestCommand: priorInput.guestCommand,
  appArguments: '--arch=x86 --renderer=cpu --react --page=data --width=950 --height=550',
  requestedDisplay: '1024x768',
  inspectScheduleMs: [10000, 15000, 20000, 25000, 30000, 35000, 40000, 45000, 50000],
  outerWheel: {count: 12, clientPoint: [900, 450], action: 'wheel',
    beforePauseMs: 8000, afterPauseMs: 8000},
  expectedSerialFrames: 61,
  closeScheduleMs: [90000, 95000, 100000, 105000, 110000],
  appDeadlineMs: 120000,
  mode: 'layout-outer-wheel-before-after-exploration-no-full-v02-input',
  guestExecuted: false, win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(output, 'input.json'), JSON.stringify(input, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({status: 'prepared', transferFiles: files.length,
  transferManifestSha256: sha(manifest), guestScriptSha256: sha(scriptBytes),
  serialScriptSha256: sha(serialBytes), serialFrameLimit: 64,
  guestExecuted: false, releaseReady: false}));
