'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { stage } = require('../../tools/stage-desktop-app.cjs');
const { archiveStage, parseArgs } = require('../../tools/archive-desktop-app.cjs');
function fixture(run, platform = 'linux') {
  const root = fs.realpathSync(fs.mkdtempSync(path.join(os.tmpdir(), 'soui-archive-')));
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
    if (platform === 'windows') {
      const pe = Buffer.alloc(512); pe.writeUInt16LE(0x5a4d); pe.writeUInt32LE(64, 60);
      pe.writeUInt32LE(0x4550, 64); pe.writeUInt16LE(0x8664, 68); pe.writeUInt16LE(240, 84);
      pe.writeUInt16LE(2, 86); pe.writeUInt16LE(0x20b, 88); pe.writeUInt16LE(3, 156);
      fs.writeFileSync(binary, pe);
    }
    const assets = path.join(root, 'assets'); fs.mkdirSync(assets);
    fs.writeFileSync(path.join(assets, 'index.html'), '<!doctype html>test');
    const output = path.join(root, 'output');
    const notices = {};
    for (const option of ['sdk-license', 'runtime-notices', ...(platform === 'windows' ? ['webview2-license', 'webview2-notices'] : [])]) {
      notices[option] = path.join(root, option); fs.writeFileSync(notices[option], `${option}\r\n原文\n`);
    }
    stage({ binary, assets, output, name: 'Example', platform, arch: 'x64', version: '1.0.0', ...notices });
    run(output, root);
  } finally { fs.rmSync(root, { recursive: true, force: true }); }
}

test('archives verified stage with standard ZIP metadata and truthful delivery claims', { skip: process.platform === 'win32' }, () => fixture((input, root) => {
  const output = path.join(root, 'delivery');
  const result = archiveStage(input, output, { platform: 'linux', arch: 'x64' });
  assert.equal(result.integrityVerified, true);
  assert.equal(result.authenticityVerified, false); assert.equal(result.nativeRuntimeVerified, false);
  const bytes = fs.readFileSync(path.join(output, result.archive));
  assert.equal(crypto.createHash('sha256').update(bytes).digest('hex'), result.sha256);
  assert.equal(bytes.readUInt32LE(bytes.length - 22), 0x06054b50);
  let at = bytes.readUInt32LE(bytes.length - 6), executable = false, directories = 0, files = 0;
  for (let i = 0; i < result.archiveEntries; ++i) {
    assert.equal(bytes.readUInt32LE(at), 0x02014b50);
    assert.equal(bytes.readUInt16LE(at + 8), 0x800); assert.equal(bytes.readUInt16LE(at + 10), 0);
    const length = bytes.readUInt16LE(at + 28), name = bytes.subarray(at + 46, at + 46 + length).toString('utf8');
    assert.ok(name.startsWith(result.archiveRoot + '/'));
    const local = bytes.readUInt32LE(at + 42);
    assert.equal(bytes.readUInt32LE(local), 0x04034b50);
    assert.equal(bytes.readUInt16LE(local + 26), length);
    if (name.endsWith('/')) ++directories; else ++files;
    if (name === result.archiveRoot + '/Example') { executable = true; assert.equal((bytes.readUInt32LE(at + 38) >>> 16) & 0o777, 0o755); }
    at += 46 + length;
  }
  assert.equal(at, bytes.length - 22); assert.ok(executable && directories >= 2);
  assert.equal(files, result.payloadFiles);
  assert.equal(fs.existsSync(path.join(output, 'delivery-manifest.json')), true);
  assert.throws(() => archiveStage(input, output), error => error.code === 'output_exists');
}));
test('refuses modified stage and wrong expected identity before creating output', () => fixture((input, root) => {
  const output = path.join(root, 'delivery');
  assert.throws(() => archiveStage(input, output, { arch: 'arm64' }), error => error.code === 'expected_identity_mismatch');
  fs.appendFileSync(path.join(input, 'web/index.html'), 'tamper');
  assert.throws(() => archiveStage(input, output), error => error.code === 'size_mismatch');
  assert.equal(fs.existsSync(output), false);
}));
test('rejects overlapping output and invalid CLI options', () => fixture((input, root) => {
  assert.throws(() => archiveStage(input, path.join(input, 'delivery')), error => error.code === 'overlapping_paths');
  assert.throws(() => parseArgs(['--input', input, '--output', 'out', '--output', 'other']));
  assert.throws(() => parseArgs(['--input', input, '--output', 'out', '--install', 'yes']));
}));
test('rejects symlink output parents', { skip: process.platform === 'win32' ? 'requires POSIX symlink fixture' : false }, () => fixture((input, root) => {
  const link = path.join(root, 'alias'); fs.symlinkSync(input, link, 'dir');
  assert.throws(() => archiveStage(input, path.join(link, 'delivery')), error => error.code === 'symlink_not_allowed');
}));

test('Windows ZIP contains the exact four notice payloads and manifest hashes', () => fixture((input, root) => {
  const output = path.join(root, 'delivery');
  const result = archiveStage(input, output, { platform: 'windows', arch: 'x64' });
  const bytes = fs.readFileSync(path.join(output, result.archive));
  const manifest = JSON.parse(fs.readFileSync(path.join(input, 'application-manifest.json')));
  assert.deepEqual(manifest.notices.files, ['legal/LICENSE.md', 'legal/THIRD_PARTY_NOTICES.md', 'legal/WebView2-LICENSE.txt', 'legal/WebView2-NOTICE.txt']);
  let at = bytes.readUInt32LE(bytes.length - 6), notices = 0;
  for (let index = 0; index < result.archiveEntries; ++index) {
    assert.equal(bytes.readUInt32LE(at), 0x02014b50);
    const length = bytes.readUInt16LE(at + 28), name = bytes.subarray(at + 46, at + 46 + length).toString('utf8');
    const relative = name.slice(result.archiveRoot.length + 1);
    if (manifest.notices.files.includes(relative)) {
      const local = bytes.readUInt32LE(at + 42), size = bytes.readUInt32LE(at + 24);
      const start = local + 30 + bytes.readUInt16LE(local + 26) + bytes.readUInt16LE(local + 28);
      const data = bytes.subarray(start, start + size), record = manifest.files.find(file => file.path === relative);
      assert.deepEqual(data, fs.readFileSync(path.join(input, relative)));
      assert.equal(crypto.createHash('sha256').update(data).digest('hex'), record.sha256); ++notices;
    }
    at += 46 + length + bytes.readUInt16LE(at + 30) + bytes.readUInt16LE(at + 32);
  }
  assert.equal(notices, 4);
}, 'windows'));
