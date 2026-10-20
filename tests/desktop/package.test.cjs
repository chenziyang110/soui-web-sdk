'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const { parseArgs, filesIn, identity, packageDesktop } = require('../../tools/package-desktop.cjs');
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-package-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const source = path.join(root, 'source');
  const build = path.join(root, 'build');
  fs.mkdirSync(source); fs.mkdirSync(build);
  fs.writeFileSync(path.join(build, 'CMakeCache.txt'), [
    `CMAKE_HOME_DIRECTORY:INTERNAL=${source}`, 'SOUI_BUILD_DESKTOP:BOOL=ON',
    'SOUI_BUILD_TESTS:BOOL=ON', 'CMAKE_BUILD_TYPE:STRING=Release',
  ].join('\n'));
  return { root, source, build, output: path.join(root, 'new output'), configuration: 'Release' };
}
function populate(stage) {
  const entries = {
    'lib/cmake/SouiRuntime/SouiRuntimeConfig.cmake': 'set(SouiRuntime_TARGET_OS "linux")\nset(SouiRuntime_TARGET_ARCH "arm64")',
    'lib/cmake/SouiDesktop/SouiDesktopConfig.cmake': 'set(SouiDesktop_BACKEND "webkitgtk")',
    'include/soui/version.h': '#define SOUI_SDK_VERSION_STRING "0.5.0"',
    'include/soui/desktop/app.h': '// public header',
    'lib/Release/libsoui_desktop.a': 'fixture, not a compiled library',
  };
  for (const [relative, content] of Object.entries(entries)) {
    const filename = path.join(stage, relative);
    fs.mkdirSync(path.dirname(filename), { recursive: true });
    fs.writeFileSync(filename, content);
  }
}
test('arguments are explicit, bounded and reject repeated/unknown options', () => {
  assert.deepEqual(parseArgs(['--build-dir', 'b', '--output', 'o']), { build: 'b', output: 'o', configuration: 'Release' });
  for (const args of [[], ['--build-dir', 'b'], ['--wat', 'x'],
    ['--build-dir', 'b', '--output', 'o', '--config', 'Release', '--config', 'Debug'],
    ['--build-dir', 'b', '--output', 'o', '--config', '../bad']]) assert.throws(() => parseArgs(args));
});
test('packaging runs build/test/install/archive, uses target metadata, and hashes exact bytes', t => {
  const f = fixture(t);
  const calls = [];
  const result = packageDesktop(f, (command, args) => {
    calls.push([command, args]);
    if (args[0] === '--install') populate(args.at(-1));
    if (args[0] === '-E') fs.writeFileSync(args[3], 'simulated archive bytes');
  }, f.source);
  assert.equal(calls.length, 4);
  assert.equal(calls[1][0], 'ctest');
  assert.ok(calls[1][1].includes('--no-tests=error'));
  assert.equal(result.manifest.architecture, 'arm64'); // Not the test runner's host CPU.
  assert.equal(result.manifest.releaseReady, false);
  assert.equal(result.manifest.validation.nativeGui, 'not-certified');
  assert.match(path.basename(result.archive), /^soui-desktop-0.5.0-dev-linux-arm64-Release-[a-f0-9]{12}\.zip$/);
  assert.equal(result.sha256, crypto.createHash('sha256').update('simulated archive bytes').digest('hex'));
  assert.ok(fs.readFileSync(`${result.archive}.sha256`, 'utf8').startsWith(result.sha256));
  assert.equal(result.manifest.files.length, 5);
});
test('existing output is never replaced and commands are not started', t => {
  const f = fixture(t); fs.mkdirSync(f.output);
  assert.throws(() => packageDesktop(f, () => assert.fail('should not execute'), f.source), /already exists/);
});
test('failed tests stop before install and preserve diagnostics without a success archive', t => {
  const f = fixture(t);
  let count = 0;
  assert.throws(() => packageDesktop(f, (command) => {
    ++count; if (command === 'ctest') throw new Error('test failed');
  }, f.source), /test failed/);
  assert.equal(count, 2);
  assert.deepEqual(JSON.parse(fs.readFileSync(path.join(f.output, 'FAILED.json'))).completed, ['build']);
  assert.deepEqual(fs.readdirSync(f.output), ['FAILED.json']);
});
test('wrong source, tests disabled and single-config mismatch fail closed', t => {
  const f = fixture(t);
  assert.throws(() => packageDesktop({ ...f, configuration: 'Debug' }, () => assert.fail(), f.source), /configuration/);
  assert.throws(() => packageDesktop(f, () => assert.fail(), f.root), /source checkout/);
  fs.appendFileSync(path.join(f.build, 'CMakeCache.txt'), '\nSOUI_BUILD_TESTS:BOOL=OFF\n');
  assert.throws(() => packageDesktop(f, () => assert.fail(), f.source), /SOUI_BUILD_TESTS/);
});
test('installed metadata mismatches or missing payload fail', t => {
  const f = fixture(t); const stage = path.join(f.root, 'stage'); populate(stage);
  fs.writeFileSync(path.join(stage, 'lib/cmake/SouiDesktop/SouiDesktopConfig.cmake'), 'set(SouiDesktop_BACKEND "wkwebview")');
  assert.throws(() => identity(stage), /mismatch/);
  assert.throws(() => packageDesktop(f, (_, args) => {
    if (args[0] === '--install') {
      populate(args.at(-1)); fs.unlinkSync(path.join(args.at(-1), 'lib/Release/libsoui_desktop.a'));
    }
  }, f.source), /library is missing/);
});
test('symlinks are rejected, not followed into external files', { skip: process.platform === 'win32' }, t => {
  const f = fixture(t);
  fs.symlinkSync(f.build, path.join(f.source, 'external'));
  assert.throws(() => filesIn(f.source), /Symlink/);
});
