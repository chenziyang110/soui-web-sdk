'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { stage } = require('../../tools/stage-desktop-app.cjs');
const { verifyStage, parseArgs } = require('../../tools/verify-desktop-app.cjs');
function fixture(run) {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'soui-verify-')));
  try {
    const binary = path.join(root, 'input');
    const bytes = Buffer.alloc(256);
    bytes.set([0x7f, 0x45, 0x4c, 0x46, 2, 1, 1]);
    bytes.writeUInt16LE(3, 16); bytes.writeUInt16LE(62, 18);
    bytes.writeBigUInt64LE(0x1000n, 24); bytes.writeBigUInt64LE(64n, 32);
    bytes.writeUInt16LE(56, 54); bytes.writeUInt16LE(2, 56);
    bytes.writeUInt32LE(1, 64); bytes.writeUInt32LE(5, 68);
    bytes.writeBigUInt64LE(0x1000n, 80); bytes.writeBigUInt64LE(64n, 104);
    bytes.writeUInt32LE(3, 120); bytes.writeBigUInt64LE(200n, 128);
    const interpreter = Buffer.from('/lib/ld-test.so\0');
    bytes.writeBigUInt64LE(BigInt(interpreter.length), 152); interpreter.copy(bytes, 200);
    fs.writeFileSync(binary, bytes);
    const assets = path.join(root, 'assets'); fs.mkdirSync(assets);
    fs.writeFileSync(path.join(assets, 'index.html'), '<!doctype html>test');
    const output = path.join(root, 'output');
    const license = path.join(root, 'LICENSE.md'), notices = path.join(root, 'THIRD_PARTY_NOTICES.md');
    fs.writeFileSync(license, 'License\r\n'); fs.writeFileSync(notices, 'Notices\n');
    stage({ binary, assets, output, name: 'Example', platform: 'linux', arch: 'x64', version: '1.0.0',
      'sdk-license': license, 'runtime-notices': notices });
    run(output, root);
  } finally { fs.rmSync(root, { recursive: true, force: true }); }
}
const rejects = (fn, code) => assert.throws(fn, error => error.code === code);
function rewrite(output, edit) {
  const file = path.join(output, 'application-manifest.json');
  const manifest = JSON.parse(fs.readFileSync(file)); edit(manifest);
  fs.writeFileSync(file, JSON.stringify(manifest));
}
test('verifies production staged inventory without authenticity or runtime claims', () => fixture(output => {
  const result = verifyStage(output, { platform: 'linux', arch: 'x64' });
  assert.equal(result.ok, true); assert.equal(result.checkedFiles, 7); assert.equal(result.noticeSetVerified, true);
  assert.equal(result.authenticityVerified, false); assert.equal(result.nativeRuntimeVerified, false);
  rejects(() => verifyStage(output, { arch: 'arm64' }), 'expected_identity_mismatch');
}));
test('detects changed and missing payload files', () => fixture(output => {
  const file = path.join(output, 'web/index.html');
  const data = fs.readFileSync(file); data[0] ^= 1; fs.writeFileSync(file, data);
  rejects(() => verifyStage(output), 'hash_mismatch');
  fs.unlinkSync(file); rejects(() => verifyStage(output), 'missing_file');
}));
test('rejects unlisted files and extra directories', () => fixture(output => {
  const extra = path.join(output, 'extra'); fs.writeFileSync(extra, 'unexpected');
  rejects(() => verifyStage(output), 'unexpected_file'); fs.unlinkSync(extra);
  fs.mkdirSync(extra); rejects(() => verifyStage(output), 'unexpected_directory'); fs.rmdirSync(extra);
}));
test('rejects symlinks', { skip: process.platform === 'win32' ? 'requires POSIX symlink fixture' : false }, () => fixture(output => {
  const extra = path.join(output, 'extra');
  fs.symlinkSync(path.join(output, 'web'), extra, process.platform === 'win32' ? 'junction' : 'dir');
  rejects(() => verifyStage(output), 'symlink_not_allowed');
}));
test('rejects manifest traversal, duplicate paths and identity mutation', () => fixture(output => {
  const original = fs.readFileSync(path.join(output, 'application-manifest.json'));
  const reset = () => fs.writeFileSync(path.join(output, 'application-manifest.json'), original);
  rewrite(output, manifest => { manifest.files[0].path = '../escape'; });
  rejects(() => verifyStage(output), 'unsafe_manifest_path'); reset();
  rewrite(output, manifest => { manifest.files.push(manifest.files[0]); });
  rejects(() => verifyStage(output), 'duplicate_path'); reset();
  rewrite(output, manifest => { manifest.architecture = 'arm64'; });
  rejects(() => verifyStage(output), 'binary_identity_mismatch');
}));
test('accepts pre-doctor schema /1 without claiming diagnostics are present', () => fixture(output => {
  rewrite(output, manifest => {
    manifest.schema = 'soui.desktop.application-stage/1'; delete manifest.notices; delete manifest.diagnostics;
    manifest.files = manifest.files.filter(file => file.path !== 'doctor.sh' && !file.path.startsWith('legal/'));
  });
  fs.unlinkSync(path.join(output, 'doctor.sh'));
  fs.unlinkSync(path.join(output, 'legal/LICENSE.md')); fs.unlinkSync(path.join(output, 'legal/THIRD_PARTY_NOTICES.md'));
  fs.rmdirSync(path.join(output, 'legal'));
  assert.equal(verifyStage(output).diagnosticsPresent, false);
  assert.equal(verifyStage(output).noticeSetVerified, false);
}));

test('v2 rejects missing records, wrong platform notice set and extra legal paths', () => fixture(output => {
  const original = fs.readFileSync(path.join(output, 'application-manifest.json'));
  const reset = () => fs.writeFileSync(path.join(output, 'application-manifest.json'), original);
  for (const [edit, code] of [
    [m => { delete m.notices; }, 'invalid_manifest'],
    [m => { m.schema = 'soui.desktop.application-stage/3'; }, 'invalid_manifest'],
    [m => { m.schema = 'soui.desktop.application-stage/1'; }, 'invalid_manifest'],
    [m => { m.schema = 'soui.desktop.application-stage/1'; delete m.notices; }, 'unsafe_manifest_path'],
    [m => { m.schema = 'soui.desktop.application-stage/1'; delete m.notices; m.files = m.files.filter(f => !f.path.startsWith('legal/')); }, 'unexpected_directory'],
    [m => { m.notices.files.push('legal/WebView2-NOTICE.txt'); }, 'invalid_notices'],
    [m => { m.notices.files[0] = 'web/LICENSE.md'; }, 'invalid_notices'],
    [m => { m.files = m.files.filter(f => f.path !== 'legal/LICENSE.md'); }, 'missing_required_file'],
    [m => { m.files.find(f => f.path === 'legal/LICENSE.md').size = 0; }, 'invalid_notice_file'],
    [m => { m.files.push({ ...m.files[0], path: 'legal/extra.txt' }); }, 'unsafe_manifest_path'],
  ]) { rewrite(output, edit); rejects(() => verifyStage(output), code); reset(); }
  const extra = path.join(output, 'legal/extra.txt'); fs.writeFileSync(extra, 'extra');
  rejects(() => verifyStage(output), 'unexpected_file'); fs.unlinkSync(extra);
  const license = path.join(output, 'legal/LICENSE.md'), bytes = fs.readFileSync(license);
  bytes[0] ^= 1; fs.writeFileSync(license, bytes); rejects(() => verifyStage(output), 'hash_mismatch');
  fs.unlinkSync(license); rejects(() => verifyStage(output), 'missing_file');
}));
test('requires owner executable bit for POSIX staged programs', { skip: process.platform === 'win32' }, () => fixture(output => {
  fs.chmodSync(path.join(output, 'Example'), 0o601);
  rejects(() => verifyStage(output), 'executable_permission_missing');
}));
test('CLI rejects duplicate and unknown options', () => {
  assert.deepEqual(parseArgs(['--input', 'app', '--platform', 'linux']), { input: 'app', platform: 'linux' });
  rejects(() => parseArgs(['--input', 'app', '--input', 'other']), 'invalid_arguments');
  rejects(() => parseArgs(['--input', 'app', '--execute', 'yes']), 'invalid_arguments');
});
