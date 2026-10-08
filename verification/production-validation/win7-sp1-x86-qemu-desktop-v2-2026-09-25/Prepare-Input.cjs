'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const evidence = require('../../../tools/internal/build-evidence.cjs');
const identity = require('../../../tools/internal/delivery-identity.cjs');

const root = path.resolve(__dirname, '../../..');
const transfer = 'D:\\SOUI-SDK-QEMU-validation\\guest-desktop-v2-transfer-20260925';
const previous = 'D:\\SOUI-SDK-QEMU-validation\\guest-desktop-transfer-20260925';
const oldInput = JSON.parse(fs.readFileSync(path.join(__dirname,
  '../win7-sp1-x86-qemu-desktop-2026-09-25/input.json'), 'utf8'));
const appInput = JSON.parse(fs.readFileSync(path.join(__dirname,
  '../win7-sp1-x86-qemu-preview6-apps-2026-09-25/input.json'), 'utf8'));
const build = evidence.verifyBuild(root, 'x86', 'Release');
if (oldInput.schema !== 'soui.qemu-desktop-input/v1' ||
    build.version !== '0.3.0-preview.6' || build.buildId !== appInput.buildId ||
    build.source.sha256 !== appInput.sourceSha256) {
  throw new Error('Unexpected frozen desktop executable or preview.6 runtime build');
}
if (fs.existsSync(transfer) || fs.existsSync(path.join(__dirname, 'input.json'))) {
  throw new Error('Desktop v2 input already exists');
}
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex').toUpperCase();
const exe = path.join(previous, 'soui_desktop_unicode_tests.exe');
if (fs.statSync(exe).size !== oldInput.testExeBytes || hash(exe) !== oldInput.testExeSha256) {
  throw new Error('Frozen desktop executable changed');
}
const app = path.join(root, 'out/x86/bin/Release/SouiStarter');
const runtimeFiles = build.payloads.SouiStarter.files.filter(file =>
  !file.path.includes('/') && /^(?:api-ms-win-.*|concrt140|msvcp140(?:_.*)?|vccorlib140|vcruntime140(?:_.*)?|ucrtbase)\.dll$/i.test(file.path));
if (runtimeFiles.length < 40) throw new Error('Incomplete app-local VC/UCRT runtime set');
for (const file of runtimeFiles) {
  if (hash(identity.safePath(app, file.path)).toLowerCase() !== file.sha256) {
    throw new Error(`Preview.6 runtime payload changed: ${file.path}`);
  }
}
const input = {...oldInput, schema: 'soui.qemu-desktop-input/v2',
  runtimeSource: {sourceCommit: appInput.sourceCommit, sourceSha256: build.source.sha256,
    buildId: build.buildId, starterPayloadSha256: build.payloads.SouiStarter.sha256},
  runtimeFiles: runtimeFiles.map(file => ({path: file.path, bytes: file.bytes, sha256: file.sha256})),
  scripts: {}};
for (const name of ['run-desktop.cmd', 'export-serial.ps1']) {
  input.scripts[name] = hash(path.join(__dirname, 'harness', name));
}
fs.mkdirSync(transfer);
fs.copyFileSync(exe, path.join(transfer, path.basename(exe)));
for (const file of runtimeFiles) {
  fs.copyFileSync(identity.safePath(app, file.path), path.join(transfer, file.path));
}
for (const name of Object.keys(input.scripts)) {
  fs.copyFileSync(path.join(__dirname, 'harness', name), path.join(transfer, name));
}
const record = Buffer.from(JSON.stringify(input, null, 2) + '\n');
fs.writeFileSync(path.join(transfer, 'input.json'), record, {flag: 'wx'});
fs.writeFileSync(path.join(__dirname, 'input.json'), record, {flag: 'wx'});
process.stdout.write(JSON.stringify({status: 'prepared', transfer,
  testExeSha256: input.testExeSha256, runtimeFiles: input.runtimeFiles.length,
  runtimeBuildId: input.runtimeSource.buildId, releaseReady: false}) + '\n');
