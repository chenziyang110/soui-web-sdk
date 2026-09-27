'use strict';

// File preparation only. Each attempt is immutable and never starts a guest or app.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const cp = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const attempt = process.argv[2];
assert.match(attempt || '', /^attempt[1-9][0-9]*$/);
const root = path.join(__dirname, attempt);
assert.equal(fs.existsSync(root), false, 'Refuse to overwrite an attempt');
const repository = 'C:/SOUI-SDK-development/preview14-scheduler';
const identity = require(path.join(repository, 'tools/internal/delivery-identity.cjs'));
const expectedSource = '15152ef430d70b8cfbb0682c2a5adb02cdbf5ca64c64e6e93252be9b0708e46d';
const expectedBuildId = 'f0c38056-058a-456e-be99-17446127f7ec';
const expectedPayloads = {
  SouiStarter: 'bbc99238c309ddc2cc8431c79170312b7444b107e250bab0702706b799470022',
  SouiWorkbench: '7ee53c78a1c5c0c2deecfbb38f70d443d3315d6d420ec756c1b5113ccc7cbffb',
};
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const write = (file, bytes) => {
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, bytes, {flag: 'wx'});
};
const recordFile = path.join(repository, 'out/x86/build-Release.json');
const buildBytes = fs.readFileSync(recordFile);
const build = JSON.parse(buildBytes.toString('utf8').replace(/^\uFEFF/, ''));
assert.equal(build.version, '0.3.0-preview.15');
assert.equal(build.architecture, 'x86');
assert.equal(build.configuration, 'Release');
assert.equal(build.buildId, expectedBuildId);
assert.equal(build.source.sha256, expectedSource);
assert.equal(build.releaseReady, false);
assert.deepEqual(Object.keys(build.payloads).sort(), Object.keys(expectedPayloads).sort());

const sevenZipSource = 'D:/SOUI-SDK-QEMU-validation/7z1900-extra/7za.exe';
const sevenZipBytes = fs.readFileSync(sevenZipSource);
assert.equal(sha(sevenZipBytes), 'ea308c76a2f927b160a143d94072b0dce232e04b751f0c6432a94e05164e716d');
const transfer = path.join(root, 'volume/soui-input');
const sevenZip = path.join(transfer, '7za.exe');
write(sevenZip, sevenZipBytes);
write(path.join(root, 'build-Release.json'), buildBytes);
const packages = {};

for (const name of ['SouiStarter', 'SouiWorkbench']) {
  const expected = build.payloads[name];
  assert.equal(expected.sha256, expectedPayloads[name]);
  const original = path.join(repository, 'out/x86/bin/Release', name);
  identity.verifyPayload(original, name, expected);
  const target = path.join(root, 'payloads', name);
  for (const entry of expected.files) {
    const source = identity.safePath(original, entry.path);
    const destination = identity.safePath(target, entry.path);
    const bytes = fs.readFileSync(source);
    assert.equal(bytes.length, entry.bytes, `Source size changed: ${name}/${entry.path}`);
    assert.equal(sha(bytes), entry.sha256, `Source SHA changed: ${name}/${entry.path}`);
    write(destination, bytes);
  }
  identity.verifyPayload(target, name, expected);
  assert.equal(identity.walk(target).length, expected.files.length);
  const legal = expected.files.filter(item => item.path.startsWith('resources/legal/'));
  assert.equal(legal.length, 5, `Expected five legal files in ${name}`);
  const payloadManifest = Buffer.from('\uFEFF' + expected.files.map(item =>
    `${item.sha256}\t${item.bytes}\t${item.path}`).join('\r\n') + '\r\n', 'utf8');
  const manifestName = `${name}-payload.tsv`;
  write(path.join(transfer, manifestName), payloadManifest);
  const zipName = `${name}.zip`;
  const zipFile = path.join(transfer, zipName);
  const create = cp.spawnSync(sevenZip, ['a', '-tzip', '-mx=0', '-mmt=1', zipFile, '.'],
    {cwd: target, encoding: 'utf8', windowsHide: true, timeout: 60000});
  write(path.join(root, `${name}-zip-create.log`), (create.stdout || '') + (create.stderr || ''));
  assert.equal(create.error, undefined);
  assert.equal(create.status, 0, `${name} ZIP creation failed`);
  const test = cp.spawnSync(sevenZip, ['t', zipFile],
    {encoding: 'utf8', windowsHide: true, timeout: 60000});
  write(path.join(root, `${name}-zip-test.log`), (test.stdout || '') + (test.stderr || ''));
  assert.equal(test.error, undefined);
  assert.equal(test.status, 0, `${name} ZIP test failed`);
  const zip = fs.readFileSync(zipFile);
  packages[name] = {
    payloadSha256: expected.sha256, payloadBytes: expected.bytes, fileCount: expected.files.length,
    legal: legal.map(item => ({path: item.path, sha256: item.sha256, bytes: item.bytes})),
    manifestName, manifestSha256: sha(payloadManifest), zipName, zipSha256: sha(zip), zipBytes: zip.length,
  };
}

for (const name of Object.keys(expectedPayloads)) {
  identity.verifyPayload(path.join(repository, 'out/x86/bin/Release', name), name, build.payloads[name]);
  identity.verifyPayload(path.join(root, 'payloads', name), name, build.payloads[name]);
}
assert.equal(sha(fs.readFileSync(recordFile)), sha(buildBytes));
write(path.join(root, 'payload-preparation.json'), JSON.stringify({
  schema: 'soui.win7-v02-current-payload-preparation/v1', attempt, version: build.version,
  sourceSha256: expectedSource, buildId: expectedBuildId, buildRecordSha256: sha(buildBytes),
  preparationScriptSha256: sha(fs.readFileSync(__filename)), packages,
  payloadPrepared: true, transportFrozen: false, guestExecuted: false, releaseReady: false,
}, null, 2) + '\n');
console.log(JSON.stringify({attempt, buildId: expectedBuildId, sourceSha256: expectedSource,
  packages: Object.fromEntries(Object.entries(packages).map(([name, item]) =>
    [name, {files: item.fileCount, zipBytes: item.zipBytes, zipSha256: item.zipSha256}])),
  payloadPrepared: true, guestExecuted: false, releaseReady: false}));
