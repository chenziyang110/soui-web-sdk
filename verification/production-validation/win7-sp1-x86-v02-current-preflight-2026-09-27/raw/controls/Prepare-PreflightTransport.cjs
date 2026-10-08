'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const attempt = path.join(root, 'attempt1');
const volume = path.join(attempt, 'volume');
const transfer = path.join(volume, 'soui-input');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const prep = readJson(path.join(attempt, 'payload-preparation.json'));
assert.equal(prep.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(prep.guestExecuted, false);
assert.equal(fs.existsSync(path.join(attempt, 'preflight-input.json')), false);
assert.equal(fs.existsSync(path.join(transfer, 'transfer-files.tsv')), false);
assert.deepEqual(fs.readdirSync(transfer).sort(), [
  '7za.exe', 'SouiStarter-payload.tsv', 'SouiStarter.zip',
  'SouiWorkbench-payload.tsv', 'SouiWorkbench.zip',
].sort());
for (const name of ['SouiStarter', 'SouiWorkbench']) {
  const zip = fs.readFileSync(path.join(transfer, `${name}.zip`));
  assert.equal(sha(zip), prep.packages[name].zipSha256);
  assert.equal(zip.length, prep.packages[name].zipBytes);
  assert.equal(sha(fs.readFileSync(path.join(transfer, `${name}-payload.tsv`))),
    prep.packages[name].manifestSha256);
}
const probeSource = 'C:/SOUI-SDK-production-validation/preview15-v02-win7-current-20260927/static-probe-attempt1/Release/V02InputProbe.exe';
const probeBytes = fs.readFileSync(probeSource);
assert.equal(sha(probeBytes), '3d7b983d9e816978748b3ea2b3fa40a0ad004ab18c01982e5c2736e71b274737');
const controls = [
  [probeSource, 'V02InputProbe.exe'],
  [path.join(root, 'harness/run-preflight.ps1'), 'run-preflight.ps1'],
  [path.join(root, 'harness/run-preflight.cmd'), 'run-preflight.cmd'],
  ['C:/SOUI-SDK-production-validation/preview15-win7-preparation-v3-20260927/harness/export-serial.ps1', 'export-serial.ps1'],
];
for (const [source, name] of controls) {
  fs.copyFileSync(source, path.join(transfer, name), fs.constants.COPYFILE_EXCL);
  assert.equal(sha(fs.readFileSync(source)), sha(fs.readFileSync(path.join(transfer, name))));
}
const entries = fs.readdirSync(transfer, {withFileTypes: true}).map(item => {
  assert.equal(item.isFile(), true, 'Transfer must contain flat files');
  assert.match(item.name, /^[A-Za-z0-9][A-Za-z0-9._-]*$/);
  const bytes = fs.readFileSync(path.join(transfer, item.name));
  return {path: item.name, bytes: bytes.length, sha256: sha(bytes)};
}).sort((a, b) => a.path.localeCompare(b.path));
const manifest = Buffer.from('\uFEFF' + entries.map(item =>
  `${item.sha256}\t${item.bytes}\t${item.path}`).join('\r\n') + '\r\n', 'utf8');
fs.writeFileSync(path.join(transfer, 'transfer-files.tsv'), manifest, {flag: 'wx'});
const input = {
  schema: 'soui.win7-v02-current-preflight-input/v1',
  version: '0.3.0-preview.15', buildId: prep.buildId, sourceSha256: prep.sourceSha256,
  buildRecordSha256: prep.buildRecordSha256,
  starterPayloadSha256: prep.packages.SouiStarter.payloadSha256,
  workbenchPayloadSha256: prep.packages.SouiWorkbench.payloadSha256,
  probeSha256: sha(probeBytes),
  transferManifestSha256: sha(manifest), transferFiles: entries,
  mountRoot: volume, inputSubdir: 'soui-input',
  guestCommand: 'cmd.exe /c D:\\soui-input\\run-preflight.cmd',
  guestTarget: 'C:\\soui-v02-current-preflight-results',
  guestApplicationLaunch: false, guestExecuted: false,
  preflightOnly: true, releaseReady: false,
};
fs.writeFileSync(path.join(attempt, 'preflight-input.json'), JSON.stringify(input, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({status: 'preflight-transport-prepared', files: entries.length,
  manifestSha256: sha(manifest), guestExecuted: false, releaseReady: false}));
