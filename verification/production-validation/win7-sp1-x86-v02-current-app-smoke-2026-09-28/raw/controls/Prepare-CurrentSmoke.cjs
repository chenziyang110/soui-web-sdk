'use strict';

// Host-only preparation for one current-source Win7 x86 Workbench smoke.
// Never mutate the prior, frozen environment-preflight input.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const old = path.join(root, 'attempt1');
const attempt = path.join(root, 'app-smoke-attempt1');
const transfer = path.join(attempt, 'volume', 'soui-input');
const legacy = 'C:/SOUI-SDK-production-validation/preview15-win7-preparation-v3-20260927/harness';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
const prep = JSON.parse(read(path.join(old, 'payload-preparation.json')));
assert.equal(prep.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(prep.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(fs.existsSync(attempt), false, 'Refuse to replace an attempt');
fs.mkdirSync(transfer, {recursive: true});
function put(source, name) {
  fs.copyFileSync(source, path.join(transfer, name), fs.constants.COPYFILE_EXCL);
  assert.equal(sha(read(source)), sha(read(path.join(transfer, name))));
}
for (const name of ['7za.exe', 'SouiStarter.zip', 'SouiStarter-payload.tsv',
  'SouiWorkbench.zip', 'SouiWorkbench-payload.tsv']) {
  put(path.join(old, 'volume', 'soui-input', name), name);
}
for (const app of ['SouiStarter', 'SouiWorkbench']) {
  assert.equal(sha(read(path.join(transfer, `${app}.zip`))), prep.packages[app].zipSha256);
  assert.equal(sha(read(path.join(transfer, `${app}-payload.tsv`))), prep.packages[app].manifestSha256);
}
for (const name of ['verify-apps.ps1', 'export-serial.ps1']) put(path.join(legacy, name), name);
let script = read(path.join(legacy, 'run-apps.ps1')).toString('utf8');
function replaceOnce(before, after) {
  assert.equal(script.split(before).length, 2, `Expected exactly one ${before}`);
  script = script.replace(before, after);
}
replaceOnce('C:\\soui-app-validation-preview15', 'C:\\soui-v02-current-app-smoke');
replaceOnce('C:\\soui-app-results-preview15', 'C:\\soui-v02-current-app-smoke-results');
replaceOnce("  Run-App 'starter' 'SouiStarter' '--smoke-test' 'smoke.log'\n", '');
replaceOnce("  Run-App 'workbench-default' 'SouiWorkbench' '--webview-smoke-test --arch=x86 --renderer=cpu' 'webview-smoke.log'\n", '');
replaceOnce("Run-App 'workbench-react' 'SouiWorkbench' '--webview-smoke-test --arch=x86 --renderer=cpu --react' 'webview-smoke.log'",
  "Run-App 'workbench-current' 'SouiWorkbench' '--webview-smoke-test --arch=x86 --renderer=cpu --react --page=data --width=700 --height=500' 'webview-smoke.log'");
replaceOnce('version=0.3.0-preview.15;buildId=b8c26d8d-7144-4eb0-b889-0034ec8029fb;source=b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07;apps=3;status=completed',
  'version=0.3.0-preview.15;buildId=f0c38056-058a-456e-be99-17446127f7ec;source=15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d;apps=1;status=completed;scope=short-smoke-not-v02-input');
fs.writeFileSync(path.join(transfer, 'run-current-smoke.ps1'), script, {flag: 'wx'});
const command = '@echo off\r\npowershell.exe -NoProfile -ExecutionPolicy Bypass -File D:\\soui-input\\run-current-smoke.ps1 -Transfer D:\\soui-input\r\nexit /b %errorlevel%\r\n';
fs.writeFileSync(path.join(transfer, 'run-current-smoke.cmd'), command, {flag: 'wx'});
const files = fs.readdirSync(transfer, {withFileTypes: true}).map(item => {
  assert.equal(item.isFile(), true);
  assert.match(item.name, /^[A-Za-z0-9][A-Za-z0-9._-]*$/);
  const bytes = read(path.join(transfer, item.name));
  return {path: item.name, bytes: bytes.length, sha256: sha(bytes)};
}).sort((a, b) => a.path.localeCompare(b.path));
const manifest = Buffer.from('\uFEFF' + files.map(item =>
  `${item.sha256}\t${item.bytes}\t${item.path}`).join('\r\n') + '\r\n', 'utf8');
fs.writeFileSync(path.join(transfer, 'transfer-files.tsv'), manifest, {flag: 'wx'});
const input = {
  schema: 'soui.win7-v02-current-app-smoke-input/v1',
  version: '0.3.0-preview.15', buildId: prep.buildId, sourceSha256: prep.sourceSha256,
  starterPayloadSha256: prep.packages.SouiStarter.payloadSha256,
  workbenchPayloadSha256: prep.packages.SouiWorkbench.payloadSha256,
  transferManifestSha256: sha(manifest), transferFiles: files,
  mountRoot: path.join(attempt, 'volume'), inputSubdir: 'soui-input',
  guestCommand: 'cmd.exe /c D:\\soui-input\\run-current-smoke.cmd',
  appArguments: '--webview-smoke-test --arch=x86 --renderer=cpu --react --page=data --width=700 --height=500',
  deadlineMs: 60000, mode: 'short-smoke-no-interaction', guestExecuted: false,
  win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'input.json'), JSON.stringify(input, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({status: 'prepared', transferFiles: files.length,
  transferManifestSha256: sha(manifest), guestExecuted: false, releaseReady: false}));
