'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const here = __dirname;
const previous = path.resolve(here, '../win7-sp1-x86-qemu-title-timeout-2026-09-25');
const runtime = path.resolve(here, '../win7-sp1-x86-qemu-desktop-v2-2026-09-25');
const base = 'D:\\SOUI-SDK-QEMU-validation';
const oldTransfer = path.join(base, 'guest-title-transfer-20260925');
const runtimeTransfer = path.join(base, 'guest-desktop-v2-transfer-20260925');
const transfer = path.join(base, 'guest-title-v2-transfer-20260925');
const oldInput = JSON.parse(fs.readFileSync(path.join(previous, 'input.json'), 'utf8'));
const runtimeInput = JSON.parse(fs.readFileSync(path.join(runtime, 'input.json'), 'utf8'));
const hash = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex').toUpperCase();

if (fs.existsSync(transfer) || fs.existsSync(path.join(here, 'input.json'))) {
  throw new Error('Title v2 input already exists');
}
if (oldInput.schema !== 'soui.qemu-title-input/v1' ||
    runtimeInput.schema !== 'soui.qemu-desktop-input/v2' ||
    runtimeInput.runtimeFiles.length !== 52 || !fs.existsSync(oldTransfer)) {
  throw new Error('Frozen title or runtime input is unavailable');
}
const exeName = 'soui_desktop_unicode_tests.exe';
const exe = path.join(oldTransfer, exeName);
if (fs.statSync(exe).size !== oldInput.testExeBytes || hash(exe) !== oldInput.testExeSha256) {
  throw new Error('Frozen title executable changed');
}
for (const entry of runtimeInput.runtimeFiles) {
  if (!/^[A-Za-z0-9_.-]+\.dll$/.test(entry.path)) throw new Error('Invalid runtime name');
  const source = path.join(runtimeTransfer, entry.path);
  if (fs.statSync(source).size !== entry.bytes || hash(source).toLowerCase() !== entry.sha256) {
    throw new Error(`Frozen runtime file changed: ${entry.path}`);
  }
}
const scripts = ['run-title.cmd', 'export-serial.ps1'];
const input = {...oldInput, schema: 'soui.qemu-title-input/v2',
  runtimeSource: runtimeInput.runtimeSource, runtimeFiles: runtimeInput.runtimeFiles,
  scripts: Object.fromEntries(scripts.map(name => [name, hash(path.join(here, 'harness', name))]))};
fs.mkdirSync(transfer);
fs.copyFileSync(exe, path.join(transfer, exeName));
for (const entry of runtimeInput.runtimeFiles) {
  fs.copyFileSync(path.join(runtimeTransfer, entry.path), path.join(transfer, entry.path));
}
for (const name of scripts) fs.copyFileSync(path.join(here, 'harness', name), path.join(transfer, name));
const record = JSON.stringify(input, null, 2) + '\n';
fs.writeFileSync(path.join(transfer, 'input.json'), record, {flag: 'wx'});
fs.writeFileSync(path.join(here, 'input.json'), record, {flag: 'wx'});
process.stdout.write(JSON.stringify({status: 'prepared', transfer,
  testExeSha256: input.testExeSha256, runtimeFiles: input.runtimeFiles.length,
  releaseReady: false}) + '\n');
