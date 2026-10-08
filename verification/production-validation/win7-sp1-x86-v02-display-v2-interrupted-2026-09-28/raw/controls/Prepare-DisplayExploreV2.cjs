'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const priorRoot = path.join(root, 'display-explore-attempt1');
const prior = path.join(priorRoot, 'volume', 'soui-input');
const output = path.join(root, 'display-explore-v2-attempt1');
const transfer = path.join(output, 'volume', 'soui-input');
assert.equal(fs.existsSync(output), false, 'Refuse to replace v2 guest input');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
assert.equal(sha(read(path.join(priorRoot, 'freeze.json'))),
  '3dcc7c9eb1dbf71fd79f1626e32192a3e4bcab020612b3a27ab6ab557782474b');
const priorInput = JSON.parse(read(path.join(priorRoot, 'input.json')));
const guestScript = path.join(root, 'display-explore-v2-source', 'run-display-explore.ps1');
const scriptBytes = read(guestScript);
const scriptText = scriptBytes.toString('utf8');
assert.match(scriptText, /C:\\soui-v02-current-display-explore-v2-results/);
assert.match(scriptText, /window-inspect-samples\.txt/);
assert.match(scriptText, /TimeoutMs=60000/);
assert.match(scriptText, /scope=1024x768-visible-route-exploration-v2-not-v02-input/);
fs.mkdirSync(transfer, {recursive: true});
for (const item of fs.readdirSync(prior, {withFileTypes: true})) {
  assert.equal(item.isFile(), true);
  if (['run-display-explore.ps1', 'transfer-files.tsv'].includes(item.name)) continue;
  const source = path.join(prior, item.name);
  const destination = path.join(transfer, item.name);
  fs.copyFileSync(source, destination, fs.constants.COPYFILE_EXCL);
  assert.equal(sha(read(source)), sha(read(destination)));
}
fs.writeFileSync(path.join(transfer, 'run-display-explore.ps1'), scriptBytes, {flag: 'wx'});
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
  schema: 'soui.win7-v02-current-display-explore-input/v2',
  version: priorInput.version, buildId: priorInput.buildId,
  sourceSha256: priorInput.sourceSha256,
  starterPayloadSha256: priorInput.starterPayloadSha256,
  workbenchPayloadSha256: priorInput.workbenchPayloadSha256,
  probeSha256: priorInput.probeSha256,
  displayProbeSha256: priorInput.displayProbeSha256,
  guestScriptSha256: sha(scriptBytes),
  transferManifestSha256: sha(manifest), transferFiles: files,
  mountRoot: path.join(output, 'volume'), inputSubdir: 'soui-input',
  guestCommand: priorInput.guestCommand,
  appArguments: priorInput.appArguments,
  requestedDisplay: '1024x768',
  inspectScheduleMs: [10000, 15000, 20000, 25000, 30000, 35000, 40000],
  closeScheduleMs: [45000, 50000, 55000],
  appDeadlineMs: 60000,
  mode: 'display-visible-route-timing-exploration-no-real-input',
  guestExecuted: false, win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(output, 'input.json'), JSON.stringify(input, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({status: 'prepared', transferFiles: files.length,
  transferManifestSha256: sha(manifest), guestScriptSha256: sha(scriptBytes),
  guestExecuted: false, releaseReady: false}));
