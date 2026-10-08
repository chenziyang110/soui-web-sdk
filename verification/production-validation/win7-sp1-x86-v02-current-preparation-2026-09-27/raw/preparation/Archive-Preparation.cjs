'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const target = path.join(repo, 'verification/production-validation/win7-sp1-x86-v02-current-preparation-2026-09-27');
assert.equal(fs.existsSync(target), false, 'Refuse to replace archived evidence');
const external = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927';
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const prep = readJson(path.join(root, 'attempt1/payload-preparation.json'));
const build = readJson(path.join(root, 'attempt1/build-Release.json'));
assert.equal(prep.payloadPrepared, true);
assert.equal(prep.transportFrozen, false);
assert.equal(prep.guestExecuted, false);
assert.equal(prep.releaseReady, false);
assert.equal(prep.sourceSha256, '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d');
assert.equal(prep.buildId, 'f0c38056-058a-456e-be99-17446127f7ec');
assert.equal(build.source.sha256, prep.sourceSha256);
assert.equal(build.buildId, prep.buildId);
assert.equal(sha(fs.readFileSync(path.join(root, 'attempt1/build-Release.json'))), prep.buildRecordSha256);
assert.equal(sha(fs.readFileSync(path.join(root, 'Prepare-CurrentPayload.cjs'))), prep.preparationScriptSha256);
const identity = require(path.join(repo, 'tools/internal/delivery-identity.cjs'));
for (const name of ['SouiStarter', 'SouiWorkbench']) {
  assert.equal(prep.packages[name].payloadSha256, build.payloads[name].sha256);
  assert.equal(prep.packages[name].fileCount, build.payloads[name].files.length);
  assert.equal(prep.packages[name].legal.length, 5);
  identity.verifyPayload(path.join(root, 'attempt1/payloads', name), name, build.payloads[name]);
  identity.verifyPayload(path.join(repo, 'out/x86/bin/Release', name), name, build.payloads[name]);
  const zip = fs.readFileSync(path.join(root, 'attempt1/volume/soui-input', `${name}.zip`));
  assert.equal(sha(zip), prep.packages[name].zipSha256);
  assert.equal(zip.length, prep.packages[name].zipBytes);
}
const probe = path.join(root, 'static-probe-attempt1/Release/V02InputProbe.exe');
const probeSha = sha(fs.readFileSync(probe));
assert.equal(probeSha, '3d7b983d9e816978748b3ea2b3fa40a0ad004ab18c01982e5c2736e71b274737');
const project = fs.readFileSync(path.join(root, 'static-probe-attempt1/V02InputProbe.vcxproj'), 'utf8');
assert.match(project, /<RuntimeLibrary>MultiThreaded<\/RuntimeLibrary>/);
for (const name of ['win7-v02-current-payload-attempt1', 'win7-static-probe-configure-attempt1',
  'win7-static-probe-build-attempt1']) {
  const result = readJson(path.join(external, 'command-logs', name, 'result.json'));
  assert.equal(result.exitCode, 0, `${name} failed`);
  assert.equal(result.timedOut, false);
  assert.equal(result.interruptedByExternal, null);
  assert.equal(result.gateError, null);
  assert.equal(result.error, null);
}
for (const name of ['win7-static-probe-dependents-attempt1',
  'win7-static-probe-headers-attempt1', 'win7-static-probe-imports-attempt1']) {
  const result = readJson(path.join(external, 'command-logs', name, 'result.json'));
  assert.equal(result.exitCode, 0, `${name} failed`);
  assert.equal(result.timedOut, false);
  assert.equal(result.error, null);
}
const dependencies = fs.readFileSync(path.join(external, 'command-logs/win7-static-probe-dependents-attempt1/stdout.log'), 'utf8');
assert.match(dependencies, /USER32\.dll/);
assert.match(dependencies, /KERNEL32\.dll/);
assert.doesNotMatch(dependencies, /VCRUNTIME|MSVCP140|api-ms-win-crt/i);
const headers = fs.readFileSync(path.join(external, 'command-logs/win7-static-probe-headers-attempt1/stdout.log'), 'utf8');
assert.match(headers, /6\.00 subsystem version/);

const pairs = [];
const add = (source, destination) => pairs.push([source, destination]);
function addTree(directory, destination) {
  for (const item of fs.readdirSync(directory, {withFileTypes: true})) {
    const source = path.join(directory, item.name);
    const relative = path.join(destination, item.name);
    if (item.isDirectory()) addTree(source, relative);
    else if (item.isFile()) add(source, relative);
    else throw Error(`Unsupported raw item: ${source}`);
  }
}
for (const item of ['Prepare-CurrentPayload.cjs', 'Archive-Preparation.cjs',
  'attempt1/payload-preparation.json', 'attempt1/build-Release.json',
  'attempt1/SouiStarter-zip-create.log', 'attempt1/SouiStarter-zip-test.log',
  'attempt1/SouiWorkbench-zip-create.log', 'attempt1/SouiWorkbench-zip-test.log',
  'attempt1/volume/soui-input/SouiStarter-payload.tsv',
  'attempt1/volume/soui-input/SouiWorkbench-payload.tsv',
  'static-probe-attempt1/Release/V02InputProbe.exe',
  'static-probe-attempt1/V02InputProbe.vcxproj']) {
  add(path.join(root, item), path.join('preparation', item));
}
for (const item of ['CMakeLists.txt', 'V02InputProbe.cpp']) {
  add(path.join(external, 'probe-build-input-v2-attempt1', item), path.join('probe-input', item));
}
for (const name of ['win7-v02-current-payload-attempt1', 'win7-static-probe-configure-attempt1',
  'win7-static-probe-build-attempt1', 'win7-static-probe-dependents-attempt1',
  'win7-static-probe-headers-attempt1', 'win7-static-probe-imports-attempt1']) {
  addTree(path.join(external, 'command-logs', name), path.join('commands', name));
}
const files = [];
for (const [source, destination] of pairs) {
  const dest = path.join(target, 'raw', destination);
  fs.mkdirSync(path.dirname(dest), {recursive: true});
  fs.copyFileSync(source, dest, fs.constants.COPYFILE_EXCL);
  const original = fs.readFileSync(source);
  assert.equal(sha(fs.readFileSync(dest)), sha(original));
  files.push({source, path: path.join('raw', destination).replaceAll('\\', '/'),
    bytes: original.length, sha256: sha(original), sourceMtimeUtc: fs.statSync(source).mtime.toISOString()});
}
files.sort((a, b) => a.path.localeCompare(b.path));
fs.writeFileSync(path.join(target, 'raw-manifest.json'), JSON.stringify({
  schema: 'soui.win7-v02-current-preparation-raw/v1', fileCount: files.length, files,
  guestExecuted: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
fs.writeFileSync(path.join(target, 'review.json'), JSON.stringify({
  schema: 'soui.win7-v02-current-preparation-review/v1',
  sourceSha256: prep.sourceSha256, buildId: prep.buildId,
  starterPayloadSha256: prep.packages.SouiStarter.payloadSha256,
  workbenchPayloadSha256: prep.packages.SouiWorkbench.payloadSha256,
  starterFiles: prep.packages.SouiStarter.fileCount,
  workbenchFiles: prep.packages.SouiWorkbench.fileCount,
  legalFilesPerPackage: 5, staticProbeSha256: probeSha,
  staticProbeDirectDependencies: ['USER32.dll', 'KERNEL32.dll'],
  staticProbeSubsystemVersion: '6.00',
  sourceFilesFrozen: false, transferFrozen: false, guestExecuted: false,
  guestCompatibilityVerified: false, win7V02Verified: false, releaseReady: false,
}, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({target, rawFiles: files.length, buildId: prep.buildId,
  staticProbeSha256: probeSha, guestExecuted: false, releaseReady: false}));
