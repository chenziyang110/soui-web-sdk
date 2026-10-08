'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const cp = require('node:child_process');
const evidence = require('../../../tools/internal/build-evidence.cjs');
const identity = require('../../../tools/internal/delivery-identity.cjs');

const root = path.resolve(__dirname, '../../..');
const base = 'D:\\SOUI-SDK-QEMU-validation';
const transfer = path.join(base, 'guest-preview6-app-transfer-20260925-v3');
const stage = path.join(base, `.guest-preview6-pack-${crypto.randomUUID()}`);
const sevenZip = 'C:\\Program Files\\7-Zip\\7z.exe';
const exporter = path.join(base, 'guest-preview5-app-transfer', '7za.exe');
const targets = ['SouiStarter', 'SouiWorkbench'];
const sha256 = file => crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex').toUpperCase();
const write = (file, bytes) => {
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, bytes, {flag: 'wx'});
};
function runSevenZip(args, cwd) {
  cp.execFileSync(sevenZip, args, {cwd, windowsHide: true, stdio: 'pipe', maxBuffer: 1024 * 1024});
}

if (!fs.existsSync(sevenZip) || !fs.existsSync(exporter)) throw new Error('7-Zip tool is missing');
if (fs.existsSync(transfer)) throw new Error(`Frozen transfer already exists: ${transfer}`);
if (!path.resolve(stage).toLowerCase().startsWith(path.resolve(base).toLowerCase() + path.sep) ||
    !path.basename(stage).startsWith('.guest-preview6-pack-')) throw new Error('Unsafe staging path');
const build = evidence.verifyBuild(root, 'x86', 'Release');
if (build.version !== '0.3.0-preview.6') throw new Error('Unexpected SDK version');
const commit = cp.execFileSync('git', ['-C', root, 'rev-parse', 'HEAD'], {encoding: 'utf8', windowsHide: true}).trim();
const input = {schema: 'soui.win7-x86-preview6-app-input/v1', version: build.version,
  sourceCommit: commit, sourceSha256: build.source.sha256, buildId: build.buildId,
  architecture: 'x86', configuration: 'Release', releaseReady: false, packages: {},
  sevenZipSha256: sha256(exporter), legal: {}};
for (const name of fs.readdirSync(path.join(root, 'third_party/licenses')).sort()) {
  input.legal[`third_party/licenses/${name}`] = sha256(path.join(root, 'third_party/licenses', name));
}
input.legal.THIRD_PARTY_NOTICES = sha256(path.join(root, 'THIRD_PARTY_NOTICES.md'));

try {
  fs.mkdirSync(stage, {recursive: false});
  fs.mkdirSync(transfer, {recursive: false});
  for (const target of targets) {
    const payload = build.payloads[target];
    const output = path.join(root, 'out', 'x86', 'bin', 'Release', target);
    const appStage = path.join(stage, target);
    for (const file of payload.files) {
      const source = identity.safePath(output, file.path);
      if (sha256(source).toLowerCase() !== file.sha256) throw new Error(`Build payload changed: ${target}/${file.path}`);
      write(identity.safePath(appStage, file.path), fs.readFileSync(source));
    }
    identity.verifyPayload(appStage, target, payload);
    const zip = path.join(transfer, `${target}.zip`);
    runSevenZip(['a', '-tzip', '-mx=7', zip, '.'], appStage);
    runSevenZip(['t', zip], appStage);
    const extracted = path.join(stage, `${target}-extracted`);
    fs.mkdirSync(extracted);
    runSevenZip(['x', '-y', `-o${extracted}`, zip], appStage);
    identity.verifyPayload(extracted, target, payload);
    input.packages[target] = {zipSha256: sha256(zip), zipBytes: fs.statSync(zip).size,
      payloadSha256: payload.sha256, files: payload.files.length, bytes: payload.bytes,
      exeSha256: sha256(path.join(output, `${target}.exe`)),
      engineSha256: sha256(path.join(output, 'node.dll')),
      souiDllSha256: sha256(path.join(output, 'soui.dll'))};
  }
  fs.copyFileSync(exporter, path.join(transfer, '7za.exe'));
  input.scripts = {};
  for (const name of ['run-preview6.cmd', 'verify-apps.ps1', 'export-serial.ps1']) {
    const source = path.join(__dirname, 'harness', name);
    input.scripts[name] = sha256(source);
    fs.copyFileSync(source, path.join(transfer, name));
  }
  const record = Buffer.from(JSON.stringify(input, null, 2) + '\n', 'utf8');
  write(path.join(transfer, 'input.json'), record);
  write(path.join(__dirname, 'input.json'), record);
  process.stdout.write(JSON.stringify({status: 'prepared', transfer, packages: input.packages,
    sourceSha256: input.sourceSha256, releaseReady: false}, null, 2) + '\n');
} finally {
  if (fs.existsSync(stage)) fs.rmSync(stage, {recursive: true});
}
