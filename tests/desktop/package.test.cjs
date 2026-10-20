'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const { parseArgs, filesIn, identity, validateTests, requiredTests, packageDesktop } = require('../../tools/package-desktop.cjs');
function registration() {
  return { kind: 'ctestInfo', tests: requiredTests.map(name => ({ name, command: ['test-fixture'], properties: [] })) };
}
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-package-'));
  t.after(() => fs.rmSync(root, { recursive: true, force: true }));
  const source = path.join(root, 'source');
  const build = path.join(root, 'build');
  fs.mkdirSync(source); fs.mkdirSync(build);
  fs.writeFileSync(path.join(source, 'VERSION'), '0.5.0\n');
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
    'lib/Release/libsoui_runtime.a': 'fixture runtime library',
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
    if (args.includes('--show-only=json-v1')) return JSON.stringify(registration());
  }, f.source);
  assert.equal(calls.length, 5);
  assert.equal(calls[1][0], 'ctest');
  assert.ok(calls[1][1].includes('--show-only=json-v1'));
  assert.ok(calls[2][1].includes('--no-tests=error'));
  assert.equal(result.manifest.architecture, 'arm64'); // Not the test runner's host CPU.
  assert.equal(result.manifest.releaseReady, false);
  assert.equal(result.manifest.validation.nativeGui, 'not-certified');
  assert.match(path.basename(result.archive), /^soui-desktop-0.5.0-dev-linux-arm64-Release-[a-f0-9]{12}\.zip$/);
  assert.equal(result.sha256, crypto.createHash('sha256').update('simulated archive bytes').digest('hex'));
  assert.ok(fs.readFileSync(`${result.archive}.sha256`, 'utf8').startsWith(result.sha256));
  assert.equal(result.manifest.files.length, 6);
  assert.deepEqual(result.manifest.validation.requiredTests, requiredTests);
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
    if (args.includes('--show-only=json-v1')) return JSON.stringify(registration());
    if (args[0] === '--install') {
      populate(args.at(-1)); fs.unlinkSync(path.join(args.at(-1), 'lib/Release/libsoui_desktop.a'));
    }
  }, f.source), /library is missing/);
});
test('required registration rejects missing, duplicate, disabled or commandless tests', () => {
  assert.deepEqual(validateTests(JSON.stringify(registration())), requiredTests);
  for (const change of [
    r => r.tests.pop(),
    r => r.tests.push(r.tests[0]),
    r => { r.tests[0].properties.push({ name: 'DISABLED', value: true }); },
    r => { r.tests[0].command = []; },
  ]) {
    const listing = registration(); change(listing);
    assert.throws(() => validateTests(JSON.stringify(listing)), /Required CTest/);
  }
  assert.throws(() => validateTests('not JSON'), /valid JSON/);
  assert.throws(() => validateTests('{}'), /incomplete/);
});
test('stale registration stops before executing tests or installing', t => {
  const f = fixture(t); const calls = [];
  assert.throws(() => packageDesktop(f, (_, args) => {
    calls.push(args);
    if (args.includes('--show-only=json-v1')) return JSON.stringify({ kind: 'ctestInfo', tests: [] });
  }, f.source), /Required CTest/);
  assert.equal(calls.length, 2);
  assert.deepEqual(fs.readdirSync(f.output), ['FAILED.json']);
});
test('multi-config selection must exist and cache regeneration cannot disable tests', t => {
  const f = fixture(t);
  fs.appendFileSync(path.join(f.build, 'CMakeCache.txt'), '\nCMAKE_CONFIGURATION_TYPES:STRING=Debug;Release\n');
  assert.throws(() => packageDesktop({ ...f, configuration: 'RelWithDebInfo' },
    () => assert.fail('must not execute'), f.source), /multi-config/);
  let calls = 0;
  assert.throws(() => packageDesktop(f, () => {
    ++calls;
    fs.appendFileSync(path.join(f.build, 'CMakeCache.txt'), '\nSOUI_BUILD_TESTS:BOOL=OFF\n');
  }, f.source), /SOUI_BUILD_TESTS/);
  assert.equal(calls, 1);
});
test('installed source version, runtime library and selected configuration must match', t => {
  for (const kind of ['version', 'runtime', 'configuration']) {
    const f = fixture(t);
    assert.throws(() => packageDesktop(f, (_, args) => {
      if (args.includes('--show-only=json-v1')) return JSON.stringify(registration());
      if (args[0] === '--install') {
        const stage = args.at(-1); populate(stage);
        if (kind === 'version') fs.writeFileSync(path.join(stage, 'include/soui/version.h'),
          '#define SOUI_SDK_VERSION_STRING "0.4.0"');
        if (kind === 'runtime') fs.unlinkSync(path.join(stage, 'lib/Release/libsoui_runtime.a'));
        if (kind === 'configuration') fs.renameSync(path.join(stage, 'lib/Release'), path.join(stage, 'lib/Debug'));
      }
    }, f.source), kind === 'version' ? /VERSION/ : /library is missing/);
    assert.ok(!fs.readdirSync(f.output).some(name => name.endsWith('.zip')));
  }
});
test('symlinks are rejected, not followed into external files', { skip: process.platform === 'win32' }, t => {
  const f = fixture(t);
  fs.symlinkSync(f.build, path.join(f.source, 'external'));
  assert.throws(() => filesIn(f.source), /Symlink/);
});
