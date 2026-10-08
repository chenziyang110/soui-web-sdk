'use strict';
// File preparation only. This script never starts an application or VM.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const cp = require('node:child_process');
const root = __dirname;
const repository = 'C:/SOUI-SDK-development/preview14-scheduler';
const sourceSha256 = 'b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07';
const buildId = 'b8c26d8d-7144-4eb0-b889-0034ec8029fb';
const hash = b => crypto.createHash('sha256').update(b).digest('hex');
const write = (p, b) => fs.writeFileSync(p, b, {flag: 'wx'});
const json = v => JSON.stringify(v, null, 2) + '\n';
const identityPath = path.join(repository, 'tools/internal/delivery-identity.cjs');
const identity = require(identityPath);
const recordPath = path.join(repository, 'out/x86/build-Release.json');
const recordBytes = fs.readFileSync(recordPath);
const build = JSON.parse(recordBytes.toString('utf8').replace(/^\uFEFF/, ''));
if (build.version !== '0.3.0-preview.15' || build.architecture !== 'x86' ||
    build.configuration !== 'Release' || build.buildId !== buildId ||
    build.source.sha256 !== sourceSha256 || build.releaseReady !== false ||
    Object.keys(build.payloads).sort().join('|') !== 'SouiStarter|SouiWorkbench') {
  throw new Error('The current compiled x86 build is not the frozen preview.15 candidate');
}
for (const name of ['transfer', 'payloads', 'input.json', 'build-Release.json']) {
  if (fs.existsSync(path.join(root, name))) throw new Error(`Refuse to overwrite ${name}`);
}
const transfer = path.join(root, 'volume/soui-input');
const payloads = path.join(root, 'payloads');
fs.mkdirSync(transfer,{recursive:true});
fs.mkdirSync(payloads);
write(path.join(root, 'build-Release.json'), recordBytes);
const sevenZipSource = 'D:/SOUI-SDK-QEMU-validation/7z1900-extra/7za.exe';
const sevenZip = fs.readFileSync(sevenZipSource);
if (hash(sevenZip) !== 'ea308c76a2f927b160a143d94072b0dce232e04b751f0c6432a94e05164e716d') {
  throw new Error('Unexpected Win7-compatible x86 7-Zip helper');
}
write(path.join(transfer, '7za.exe'), sevenZip);
const packages = {};
for (const name of ['SouiStarter', 'SouiWorkbench']) {
  const expected = build.payloads[name];
  const original = path.join(repository, 'out/x86/bin/Release', name);
  identity.verifyPayload(original, name, expected);
  const target = path.join(payloads, name);
  fs.mkdirSync(target);
  for (const entry of expected.files) {
    const from = identity.safePath(original, entry.path);
    const to = identity.safePath(target, entry.path);
    fs.mkdirSync(path.dirname(to), {recursive: true});
    const bytes = fs.readFileSync(from);
    if (bytes.length !== entry.bytes || hash(bytes) !== entry.sha256) throw new Error(`Source changed: ${name}/${entry.path}`);
    write(to, bytes);
  }
  identity.verifyPayload(target, name, expected);
  if (identity.walk(target).length !== expected.files.length) throw new Error('Copied file set differs');
  const manifest = Buffer.from('\uFEFF' + expected.files.map(f => `${f.sha256}\t${f.bytes}\t${f.path}`).join('\r\n') + '\r\n', 'utf8');
  const manifestName = `${name}-payload.tsv`;
  write(path.join(transfer, manifestName), manifest);
  const zipName = `${name}.zip`;
  const zipped = cp.spawnSync(path.join(transfer, '7za.exe'), ['a', '-tzip', '-mx=0', '-mmt=1', path.join(transfer, zipName), '.'],
    {cwd: target, encoding: 'utf8', windowsHide: true, timeout: 60000});
  write(path.join(root, `${name}-zip-create.log`), (zipped.stdout || '') + (zipped.stderr || ''));
  if (zipped.error || zipped.status !== 0) throw new Error(`ZIP preparation failed: ${name}: ${zipped.error || zipped.status}`);
  const tested = cp.spawnSync(path.join(transfer, '7za.exe'), ['t', path.join(transfer, zipName)],
    {encoding: 'utf8', windowsHide: true, timeout: 60000});
  write(path.join(root, `${name}-zip-test.log`), (tested.stdout || '') + (tested.stderr || ''));
  if (tested.error || tested.status !== 0) throw new Error(`ZIP test failed: ${name}`);
  const zip = fs.readFileSync(path.join(transfer, zipName));
  const findHash = p => expected.files.find(f => f.path === p).sha256;
  const legal = expected.files.filter(f => f.path.startsWith('resources/legal/'));
  if (legal.length !== 5) throw new Error(`Expected five actual legal files: ${name}`);
  packages[name] = {zipName, zipBytes: zip.length, zipSha256: hash(zip),
    payloadSha256: expected.sha256, payloadBytes: expected.bytes,
    manifestName, manifestSha256: hash(manifest), fileCount: expected.files.length,
    exeSha256: findHash(`${name}.exe`), engineSha256: findHash('node.dll'), souiDllSha256: findHash('soui.dll'),
    files: expected.files};
}
for (const name of ['run-preview15.cmd', 'run-apps.ps1', 'verify-apps.ps1', 'export-serial.ps1']) {
  write(path.join(transfer, name), fs.readFileSync(path.join(root, 'harness', name)));
}
function fileRecord(name) {
  const bytes = fs.readFileSync(path.join(transfer, name));
  return {path: name, bytes: bytes.length, sha256: hash(bytes)};
}
let transferFiles = fs.readdirSync(transfer).sort().map(fileRecord);
write(path.join(transfer, 'transfer-files.tsv'), '\uFEFF' + transferFiles.map(f => `${f.sha256}\t${f.bytes}\t${f.path}`).join('\r\n') + '\r\n');
transferFiles = fs.readdirSync(transfer).sort().map(fileRecord);
const input = {
  schema: 'soui.win7-x86-preview15-compiled-apps-input/v1',
  version: build.version, buildId, sourceSha256,
  buildRecordSha256: hash(recordBytes), architecture: 'x86', configuration: 'Release',
  candidateKind: 'existing-compiled-candidate', sourceReproductionClaim: false,
  releaseReady: false, guestExecuted: false, preparedAt: new Date().toISOString(),
  preparationHelperSha256: hash(fs.readFileSync(identityPath)),
  guestTarget: 'C:\\soui-app-validation-preview15', guestResults: 'C:\\soui-app-results-preview15',
  appTimeoutMs: 60000, diagnosticsVersion:2, layoutVersion:3, inputSubdir:'soui-input', mountRoot:path.join(root,'volume'),
  harnessParentFreezeSha256:'10c9a4e90cf5e25a2c98d0f6107eb7881a5ab39caf768973d761a1045566826d', closeDiagnostics:{atMs:55000,deadlineMs:60000,threadLimit:128,kind:'read-only-process-thread-snapshot'}, packages, transferFiles,
  runs: [
    {name: 'starter', application: 'SouiStarter', arguments: '--smoke-test', logName: 'smoke.log'},
    {name: 'workbench-default', application: 'SouiWorkbench', arguments: '--webview-smoke-test --arch=x86 --renderer=cpu', logName: 'webview-smoke.log'},
    {name: 'workbench-react', application: 'SouiWorkbench', arguments: '--webview-smoke-test --arch=x86 --renderer=cpu --react', logName: 'webview-smoke.log'}
  ]
};
write(path.join(root, 'input.json'), json(input));
// Recheck source and copied sets after packaging to catch preparation-time drift.
for (const name of Object.keys(packages)) {
  identity.verifyPayload(path.join(repository, 'out/x86/bin/Release', name), name, build.payloads[name]);
  identity.verifyPayload(path.join(payloads, name), name, build.payloads[name]);
}
if (hash(fs.readFileSync(recordPath)) !== input.buildRecordSha256) throw new Error('Build record changed during preparation');
process.stdout.write(json({status: 'prepared-not-executed', buildId, sourceSha256,
  applications: Object.fromEntries(Object.entries(packages).map(([n, p]) => [n, {files: p.fileCount, zipBytes: p.zipBytes, zipSha256: p.zipSha256}])), releaseReady: false}));
