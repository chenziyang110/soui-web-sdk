'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const { parseArgs, stage, inspectBinary } = require('../../tools/stage-desktop-app.cjs');
function fixture(fn) {
  const dir = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-app-stage-'));
  try {
    const binary = path.join(dir, 'input');
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
    const assets = path.join(dir, 'assets'); fs.mkdirSync(assets);
    fs.writeFileSync(path.join(assets, 'index.html'), '<!doctype html>test');
    fn({ binary, assets, output: path.join(dir, 'output'), name: 'Example', platform: 'linux', arch: 'x64', version: '1.0.0' });
  } finally { fs.rmSync(dir, { recursive: true, force: true }); }
}
test('stages synthetic ELF metadata without pretending runtime acceptance', () => fixture(options => {
  const manifest = stage(options);
  assert.equal(manifest.nativeRuntimeVerified, false);
  assert.equal(manifest.architecture, 'x64');
  assert.equal(fs.readFileSync(path.join(options.output, 'web/index.html'), 'utf8'), '<!doctype html>test');
  assert.match(fs.readFileSync(path.join(options.output, 'launch.sh'), 'utf8'), /exec "\$HERE\/Example" "\$@"/);
  assert.equal(manifest.files.length, 5);
  assert.equal(manifest.diagnostics.entry, 'doctor.sh');
  assert.throws(() => stage(options), /already exists/);
}));
test('rejects metadata mismatch before creating output', () => fixture(options => {
  assert.throws(() => stage({ ...options, arch: 'arm64' }), /does not match/);
  assert.equal(fs.existsSync(options.output), false);
}));
test('rejects symlink and case-colliding assets', () => fixture(options => {
  fs.symlinkSync('index.html', path.join(options.assets, 'link'));
  assert.throws(() => stage(options), /symlinks/);
  fs.unlinkSync(path.join(options.assets, 'link'));
  fs.writeFileSync(path.join(options.assets, 'INDEX.html'), 'collision');
  assert.throws(() => stage(options), /collide/);
}));
test('rejects overlap and reserved asset names', () => fixture(options => {
  assert.throws(() => stage({ ...options, output: path.join(options.assets, 'output') }), /overlap/);
  fs.writeFileSync(path.join(options.assets, 'CON.txt'), 'bad');
  assert.throws(() => stage(options), /non-portable/);
}));
test('CLI requires portable names, supported architectures and explicit macOS identity', () => {
  const args = ['--binary', 'a', '--assets', 'b', '--output', 'c', '--name', 'Example', '--platform', 'macos', '--arch', 'arm64', '--version', '1.0.0'];
  assert.throws(() => parseArgs(args), /identifier/);
  assert.equal(parseArgs([...args, '--identifier', 'com.example.app']).identifier, 'com.example.app');
  assert.throws(() => parseArgs([...args, '--name', 'Other']), /repeated/);
});

test('module entry rejects traversal and invalid options before writing', () => fixture(options => {
  const escaped = path.join(path.dirname(options.output), 'outside');
  assert.throws(() => stage({ ...options, name: '../outside' }), /portable ASCII name/);
  assert.equal(fs.existsSync(escaped), false);
  assert.equal(fs.existsSync(options.output), false);
  for (const change of [
    { name: 'Bad"Name' }, { name: 123 }, { version: 123 },
    { platform: 'unknown' }, { arch: 'unknown' }, { identifier: 123 },
    { binary: null }, { assets: '' }, { output: `${options.output}\0` },
  ]) {
    assert.throws(() => stage({ ...options, ...change }));
    assert.equal(fs.existsSync(options.output), false);
  }
}));

test('CLI and module identifier bounds match the staged manifest verifier', () => fixture(options => {
  const identifier = `${'a'.repeat(4094)}.b`;
  const args = Object.entries({ ...options, identifier }).flatMap(([name, value]) => [`--${name}`, value]);
  assert.equal(parseArgs(args).identifier.length, 4096);
  assert.throws(() => parseArgs([...args.slice(0, -1), `${identifier}b`]), /identifier/);
  assert.throws(() => stage({ ...options, identifier: `${identifier}b` }), /identifier/);
  assert.equal(fs.existsSync(options.output), false);
  stage({ ...options, identifier });
  assert.equal(require('../../tools/verify-desktop-app.cjs').verifyStage(options.output).ok, true);
}));

test('rejects non-Linux ELF and ET_DYN without interpreter', () => fixture(options => {
  const original = fs.readFileSync(options.binary);
  const foreign = Buffer.from(original); foreign[7] = 9;
  fs.writeFileSync(options.binary, foreign);
  assert.throws(() => inspectBinary(options.binary), /OSABI/);
  const library = Buffer.from(original); library.writeUInt32LE(0, 120);
  fs.writeFileSync(options.binary, library);
  assert.throws(() => inspectBinary(options.binary), /PT_INTERP/);
}));
test('Mach-O platform and minimum OS come from bounded load commands', () => fixture(options => {
  const bytes = Buffer.alloc(64);
  bytes.writeUInt32LE(0xfeedfacf, 0); bytes.writeUInt32LE(0x100000c, 4);
  bytes.writeUInt32LE(2, 12); bytes.writeUInt32LE(1, 16); bytes.writeUInt32LE(24, 20);
  bytes.writeUInt32LE(0x32, 32); bytes.writeUInt32LE(24, 36);
  bytes.writeUInt32LE(2, 40); bytes.writeUInt32LE(14 * 65536 + 2 * 256, 44);
  fs.writeFileSync(options.binary, bytes);
  assert.throws(() => inspectBinary(options.binary), /not macOS/);
  bytes.writeUInt32LE(1, 40); fs.writeFileSync(options.binary, bytes);
  assert.equal(inspectBinary(options.binary).minimumOSVersion, '14.2.0');
  bytes.writeUInt32LE(0, 32); fs.writeFileSync(options.binary, bytes);
  assert.throws(() => inspectBinary(options.binary), /unambiguous/);
}));

test('PE rejects EFI/native subsystems rather than labeling them Windows apps', () => fixture(options => {
  const bytes = Buffer.alloc(512); bytes.writeUInt16LE(0x5a4d, 0); bytes.writeUInt32LE(64, 60);
  bytes.writeUInt32LE(0x4550, 64); bytes.writeUInt16LE(0x8664, 68);
  bytes.writeUInt16LE(240, 84); bytes.writeUInt16LE(2, 86); bytes.writeUInt16LE(0x20b, 88);
  bytes.writeUInt16LE(10, 156); fs.writeFileSync(options.binary, bytes);
  assert.throws(() => inspectBinary(options.binary), /subsystem/);
  bytes.writeUInt16LE(3, 156); fs.writeFileSync(options.binary, bytes);
  assert.equal(inspectBinary(options.binary).platform, 'windows');
}));
