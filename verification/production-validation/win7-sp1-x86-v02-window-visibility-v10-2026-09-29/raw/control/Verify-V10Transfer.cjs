'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const name = process.argv[2];
assert.match(name || '', /^win7-v10-transfer-attempt[1-9][0-9]*$/);
const root = path.join(__dirname, name);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const encode = value => JSON.stringify(value, null, 2) + '\n';
const freezeBytes = fs.readFileSync(path.join(root, 'freeze.json'));
const freeze = JSON.parse(freezeBytes);
const inputBytes = fs.readFileSync(path.join(root, 'input.json'));
const input = JSON.parse(inputBytes);
assert.equal(freeze.schema, 'soui.win7-v02-window-visibility-v10-freeze/v1');
assert.equal(input.schema, 'soui.win7-v02-window-visibility-v10-input/v1');
assert.equal(freeze.inputSha256, sha(inputBytes));
assert.equal(freeze.filesSha256, sha(Buffer.from(encode(freeze.files))));
assert.equal(freeze.sourceSha256,
  '60d729c8c0fe55dcabd72bf3a1aa2255af42017c10c63151a880fecd7d511640');
assert.equal(freeze.buildId, 'b6caec6c-117e-49eb-86fa-4182e64d5b55');
assert.equal(freeze.guestExecuted, false);
assert.equal(freeze.releaseReady, false);
assert.equal(input.minimumSerialFrames, 25);
assert.equal(input.serialFrameLimit, 128);
assert.equal(input.appDeadlineMs, 120000);
assert.equal(input.requestedDisplay, '1024x768');
assert.equal(input.guestCommand, 'cmd.exe /c D:\\soui-input\\run-display-explore.cmd');
assert.equal(path.resolve(input.mountRoot), path.join(root, 'volume'));
assert.equal(input.inputSubdir, 'soui-input');
assert.equal(input.previousTransportFreezeSha256,
  '98acf9d01407a3c4bcbe843b6d9289598d607f3c32e20485759e6fc4b1dc6e80');
assert.equal(input.appArguments,
  '--arch=x86 --renderer=cpu --react --page=data --width=850 --height=640');
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
const transfer = path.join(root, 'volume/soui-input');
assert.deepEqual(fs.readdirSync(transfer).sort(),
  [...input.transferFiles.map(item => item.path), 'transfer-files.tsv'].sort());
for (const item of input.transferFiles) {
  const bytes = fs.readFileSync(path.join(transfer, item.path));
  assert.equal(bytes.length, item.bytes, item.path);
  assert.equal(sha(bytes), item.sha256, item.path);
}
assert.equal(sha(fs.readFileSync(path.join(transfer, 'transfer-files.tsv'))),
  input.transferManifestSha256);
assert.equal(sha(fs.readFileSync(path.join(transfer, 'run-display-explore.ps1'))),
  input.guestScriptSha256);
assert.equal(input.guestScriptSha256, '585f5c614fda1cbd7a9d183013da0c15a0cd88de8bd61712eaabd304f15cd84a');
assert.equal(sha(fs.readFileSync(path.join(transfer, 'export-serial.ps1'))),
  input.serialScriptSha256);
assert.equal(sha(fs.readFileSync(path.join(transfer, 'V02InputProbe.exe'))),
  input.probeSha256);
assert.match(input.payloadName, /^win7-v9-payload-attempt[1-9][0-9]*$/);
const payload = path.join(__dirname, input.payloadName, 'payload-preparation.json');
assert.equal(sha(fs.readFileSync(payload)), input.payloadPreparationSha256);
const prepared = JSON.parse(fs.readFileSync(payload, 'utf8'));
assert.equal(prepared.schema, 'soui.win7-v02-workarea-v9-payload-preparation/v1');
assert.equal(prepared.sourceSha256, input.sourceSha256);
assert.equal(prepared.buildId, input.buildId);
assert.equal(prepared.payloadPrepared, true);
assert.equal(prepared.guestExecuted, false);
const buildBytes = fs.readFileSync(path.join(__dirname, input.payloadName, 'build-Release.json'));
assert.equal(sha(buildBytes), prepared.buildRecordSha256);
const build = JSON.parse(buildBytes.toString('utf8').replace(/^\uFEFF/, ''));
assert.equal(build.schema, 'soui.source-build/v2');
assert.equal(build.nativeBuild, 'passed');
assert.equal(build.source.sha256, input.sourceSha256);
assert.equal(build.buildId, input.buildId);
assert.equal(build.architecture, 'x86');
assert.equal(build.configuration, 'Release');
assert.equal(build.releaseReady, false);
for (const [name, field] of [['SouiStarter', 'starterPayloadSha256'],
  ['SouiWorkbench', 'workbenchPayloadSha256']]) {
  const item = prepared.packages[name];
  assert.equal(item.payloadSha256, input[field]);
  assert.equal(build.payloads[name].sha256, item.payloadSha256);
  assert.equal(item.legal.length, 5);
  assert.equal(item.fileCount, name === 'SouiStarter' ? 71 : 475);
  assert.equal(sha(fs.readFileSync(path.join(transfer, item.zipName))), item.zipSha256);
  assert.equal(sha(fs.readFileSync(path.join(transfer, item.manifestName))), item.manifestSha256);
}
const guest = fs.readFileSync(path.join(transfer, 'run-display-explore.ps1'), 'utf8');
for (const marker of [
  'window-inspect-samples.txt', 'SmokeBase64=', 'MainWindowHandle=',
  '$nextInspectMs=10000;$nextCloseMs=108000',
  '$watch.ElapsedMilliseconds -lt 108000',
  'scope=1024x768-v02-window-visibility-diagnostic-no-input-not-production',
]) assert(guest.includes(marker), `Missing guest marker: ${marker}`);
assert.equal(/\bRun-Action\s+\$pidValue/.test(guest), false);
assert.equal(input.mode, 'read-only-window-visibility-diagnostic-no-input-not-production');
console.log(JSON.stringify({status: 'verified', files: freeze.files.length,
  freezeSha256: sha(freezeBytes), win7CurrentV02Verified: false, releaseReady: false}));
