'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const source = path.join(root, 'visible-explore-attempt1', 'volume', 'soui-input');
const output = path.join(root, 'display-explore-attempt1');
const transfer = path.join(output, 'volume', 'soui-input');
assert.equal(fs.existsSync(output), false, 'Refuse to replace display exploration input');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
let guest = read(path.join(source, 'run-visible-explore.ps1')).toString('utf8');
function replaceOnce(before, after) {
  assert.equal(guest.split(before).length, 2, `Expected exactly one ${before}`);
  guest = guest.replace(before, after);
}
replaceOnce('C:\\soui-v02-current-visible-explore-results', 'C:\\soui-v02-current-display-explore-results');
replaceOnce('C:\\soui-v02-current-visible-explore', 'C:\\soui-v02-current-display-explore');
replaceOnce("  $script:trace.Add('transfer verified')",
  ["  $script:trace.Add('transfer verified')",
    "  & (Join-Path $Transfer 'V02DisplayModeProbe.exe') --set-1024x768 > (Join-Path $Results 'display-mode.log') 2>&1",
    '  $displayCode=$LASTEXITCODE',
    "  Write-New 'display-mode-exitcode.txt' ([string]$displayCode)",
    "  if($displayCode -ne 0){throw ('Display change failed: '+$displayCode)}",
    "  $script:trace.Add('1024x768 display verified')"].join('\n'));
replaceOnce('scope=visible-route-exploration-not-v02-input',
  'scope=1024x768-visible-route-exploration-not-v02-input');
fs.mkdirSync(transfer, {recursive: true});
for (const name of ['7za.exe', 'SouiStarter.zip', 'SouiStarter-payload.tsv',
  'SouiWorkbench.zip', 'SouiWorkbench-payload.tsv', 'verify-apps.ps1',
  'export-serial.ps1', 'V02InputProbe.exe']) {
  fs.copyFileSync(path.join(source, name), path.join(transfer, name), fs.constants.COPYFILE_EXCL);
  assert.equal(sha(read(path.join(source, name))), sha(read(path.join(transfer, name))));
}
const displayExe = path.join(root, 'display-probe-build-attempt1', 'Release', 'V02DisplayModeProbe.exe');
assert.equal(sha(read(displayExe)), '9fe2a3c5ca8ab983c7cd01338b25a6b1d80844c6637f8ef94403d850a17708a2');
fs.copyFileSync(displayExe, path.join(transfer, 'V02DisplayModeProbe.exe'), fs.constants.COPYFILE_EXCL);
fs.writeFileSync(path.join(transfer, 'run-display-explore.ps1'), guest, {flag: 'wx'});
fs.writeFileSync(path.join(transfer, 'run-display-explore.cmd'),
  '@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\\soui-input\\run-display-explore.ps1 -Transfer D:\\soui-input\r\nexit /b %errorlevel%\r\n', {flag: 'wx'});
const files = fs.readdirSync(transfer, {withFileTypes: true}).map(item => {
  assert.equal(item.isFile(), true);
  assert.match(item.name, /^[A-Za-z0-9][A-Za-z0-9._-]*$/);
  const bytes = read(path.join(transfer, item.name));
  return {path: item.name, bytes: bytes.length, sha256: sha(bytes)};
}).sort((a, b) => a.path.localeCompare(b.path));
const manifest = Buffer.from('\uFEFF' + files.map(item =>
  `${item.sha256}\t${item.bytes}\t${item.path}`).join('\r\n') + '\r\n', 'utf8');
fs.writeFileSync(path.join(transfer, 'transfer-files.tsv'), manifest, {flag: 'wx'});
const prior = JSON.parse(read(path.join(root, 'visible-explore-attempt1', 'input.json')));
const input = {
  schema: 'soui.win7-v02-current-display-explore-input/v1',
  version: prior.version, buildId: prior.buildId, sourceSha256: prior.sourceSha256,
  starterPayloadSha256: prior.starterPayloadSha256,
  workbenchPayloadSha256: prior.workbenchPayloadSha256,
  probeSha256: prior.probeSha256, displayProbeSha256: sha(read(displayExe)),
  transferManifestSha256: sha(manifest), transferFiles: files,
  mountRoot: path.join(output, 'volume'), inputSubdir: 'soui-input',
  guestCommand: 'cmd.exe /c D:\\soui-input\\run-display-explore.cmd',
  appArguments: prior.appArguments,
  requestedDisplay: '1024x768', inspectAfterMs: prior.inspectAfterMs,
  closeAfterMs: prior.closeAfterMs, closeWaitMs: prior.closeWaitMs,
  mode: 'display-and-visible-route-exploration-no-real-input', guestExecuted: false,
  win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(output, 'input.json'), JSON.stringify(input, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({status: 'display-exploration-input-prepared', transferFiles: files.length,
  transferManifestSha256: sha(manifest), guestExecuted: false, releaseReady: false}));
