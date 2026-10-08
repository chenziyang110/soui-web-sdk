'use strict';

// Compose a diagnostic transfer from the verified V9 payload and transport.
// It does not launch a guest or change the previously frozen V9 input.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const payloadName = 'win7-v9-payload-attempt1';
const transferName = process.argv[2];
assert.match(transferName || '', /^win7-v10-transfer-attempt[1-9][0-9]*$/);
const payloadRoot = path.join(root, payloadName);
const output = path.join(root, transferName);
assert.equal(fs.existsSync(output), false, 'Refuse to replace transfer attempt');
const oldRoot = path.join(root, 'win7-v9-transfer-attempt1');
const oldInput = JSON.parse(fs.readFileSync(path.join(oldRoot, 'input.json')));
const oldTransfer = path.join(oldRoot, 'volume/soui-input');
const oldFreezeSha = '98acf9d01407a3c4bcbe843b6d9289598d607f3c32e20485759e6fc4b1dc6e80';
const sourceSha = '60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640';
const buildId = 'b6caec6c-117e-49eb-86fa-4182e64d5b55';
const probeSha = '2ad0e9eb92c7ef7b2ba8e72c672db58fd9fa250f5dbbb1698795c359cb0e5d96';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(file);
const write = (file, bytes) => {
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, bytes, {flag: 'wx'});
};
assert.equal(sha(read(path.join(oldRoot, 'freeze.json'))), oldFreezeSha);
assert.equal(oldInput.schema, 'soui.win7-v02-workarea-v9-input/v1');
for (const item of oldInput.transferFiles) {
  assert.equal(sha(read(path.join(oldTransfer, item.path))), item.sha256,
    `Historical transport identity changed: ${item.path}`);
}
const prepared = JSON.parse(read(path.join(payloadRoot, 'payload-preparation.json')));
assert.equal(prepared.schema, 'soui.win7-v02-workarea-v9-payload-preparation/v1');
assert.equal(prepared.sourceSha256, sourceSha);
assert.equal(prepared.buildId, buildId);
assert.equal(prepared.payloadPrepared, true);
assert.equal(prepared.guestExecuted, false);
for (const name of ['SouiStarter', 'SouiWorkbench']) {
  const item = prepared.packages[name];
  assert.equal(sha(read(path.join(payloadRoot, 'volume/soui-input', item.zipName))), item.zipSha256);
  assert.equal(sha(read(path.join(payloadRoot, 'volume/soui-input', item.manifestName))), item.manifestSha256);
}
const script = read(path.join(root, 'win7-v10-source/run-visibility-diagnostic.ps1'));
const text = script.toString('utf8');
for (const marker of [
  'C:\\soui-v02-window-visibility-v10-results',
  'SmokeBase64=', 'MainWindowHandle=',
  '$nextInspectMs=10000;$nextCloseMs=108000',
  '$watch.ElapsedMilliseconds -lt 108000',
  'scope=1024x768-v02-window-visibility-diagnostic-no-input-not-production',
  '--width=850 --height=640', 'TimeoutMs=120000',
]) assert(text.includes(marker), `Missing V10 diagnostic marker: ${marker}`);
assert.equal(/\bRun-Action\s+\$pidValue/.test(text), false, 'Diagnostic must not send input actions');
assert.equal(sha(script), '585f5c614fda1cbd7a9d183013da0c15a0cd88de8bd61712eaabd304f15cd84a');
const source = path.join(root, 'win7-v9-source');
const serialScript = read(path.join(source, 'export-serial.ps1'));
assert.match(serialScript.toString('utf8'), /if\(\$files\.Count -gt 128\)/);
const transfer = path.join(output, 'volume/soui-input');
for (const name of ['7za.exe', 'run-display-explore.cmd', 'V02DisplayModeProbe.exe',
  'V02InputProbe.exe', 'verify-apps.ps1']) {
  const expected = oldInput.transferFiles.find(item => item.path === name);
  assert(expected, `Missing verified V9 input ${name}`);
  const bytes = read(path.join(oldTransfer, name));
  assert.equal(sha(bytes), expected.sha256);
  write(path.join(transfer, name), bytes);
}
assert.equal(sha(read(path.join(transfer, 'V02InputProbe.exe'))), probeSha);
for (const name of ['SouiStarter', 'SouiWorkbench']) {
  const item = prepared.packages[name];
  for (const file of [item.zipName, item.manifestName]) {
    write(path.join(transfer, file), read(path.join(payloadRoot, 'volume/soui-input', file)));
  }
}
write(path.join(transfer, 'run-display-explore.ps1'), script);
write(path.join(transfer, 'export-serial.ps1'), serialScript);
const files = fs.readdirSync(transfer, {withFileTypes: true}).map(entry => {
  assert.equal(entry.isFile(), true);
  assert.match(entry.name, /^[A-Za-z0-9][A-Za-z0-9._-]*$/);
  const bytes = read(path.join(transfer, entry.name));
  return {path: entry.name, bytes: bytes.length, sha256: sha(bytes)};
}).sort((a, b) => a.path.localeCompare(b.path));
assert.equal(files.length, 11);
const manifest = Buffer.from('\uFEFF' + files.map(item =>
  `${item.sha256}\t${item.bytes}\t${item.path}`).join('\r\n') + '\r\n', 'utf8');
write(path.join(transfer, 'transfer-files.tsv'), manifest);
const input = {
  schema: 'soui.win7-v02-window-visibility-v10-input/v1', version: prepared.version,
  buildId, sourceSha256: sourceSha,
  starterPayloadSha256: prepared.packages.SouiStarter.payloadSha256,
  workbenchPayloadSha256: prepared.packages.SouiWorkbench.payloadSha256,
  payloadName, payloadPreparationSha256: sha(read(path.join(payloadRoot, 'payload-preparation.json'))),
  previousTransportFreezeSha256: oldFreezeSha, probeSha256: probeSha,
  guestScriptSha256: sha(script), serialScriptSha256: sha(serialScript),
  serialFrameLimit: 128, minimumSerialFrames: 25,
  transferManifestSha256: sha(manifest), transferFiles: files,
  mountRoot: path.join(output, 'volume'), inputSubdir: 'soui-input',
  guestCommand: 'cmd.exe /c D:\\soui-input\\run-display-explore.cmd',
  appArguments: '--arch=x86 --renderer=cpu --react --page=data --width=850 --height=640',
  requestedDisplay: '1024x768', appDeadlineMs: 120000,
  mode: 'read-only-window-visibility-diagnostic-no-input-not-production',
  guestExecuted: false, win7CurrentV02Verified: false, releaseReady: false,
};
write(path.join(output, 'input.json'), JSON.stringify(input, null, 2) + '\n');
console.log(JSON.stringify({status: 'prepared', transferFiles: files.length,
  inputSha256: sha(read(path.join(output, 'input.json'))),
  transferManifestSha256: sha(manifest), guestExecuted: false}));
