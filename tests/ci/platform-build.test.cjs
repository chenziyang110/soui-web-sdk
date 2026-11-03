'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const cp = require('node:child_process');
let spawnAttempts = 0;
const savedSpawn = cp.spawnSync;
cp.spawnSync = () => { spawnAttempts++; throw new Error('CI metadata tests cannot start tools'); };
const { assertPrefix, runtimeTestsPassed } = require('../../tools/ci/platform-build.cjs');
cp.spawnSync = savedSpawn;

const fixture = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-ci-cache-'));
const prefix = path.join(fixture, 'installed sdk');
const packages = ['SouiRuntime', 'SouiDesktop'];
for (const name of packages) fs.mkdirSync(path.join(prefix, 'lib/cmake', name), { recursive: true });
const foreign = path.join(fixture, 'source-fallback');
fs.mkdirSync(foreign);
const cache = path.join(fixture, 'CMakeCache.txt');
function writeCache(types, locations = {}) {
  fs.writeFileSync(cache, packages.map((name, index) =>
    name + '_DIR:' + types[index] + '=' + (locations[name] || path.join(prefix, 'lib/cmake', name))).join('\n') + '\n');
}
test.after(() => {
  assert.equal(spawnAttempts, 0);
  const base = fs.realpathSync(os.tmpdir()), target = fs.realpathSync(fixture);
  assert(target.startsWith(base + path.sep) && path.basename(target).startsWith('soui-ci-cache-'));
  fs.rmSync(target, { recursive: true });
});

for (const types of [['PATH', 'PATH'], ['UNINITIALIZED', 'UNINITIALIZED'], ['PATH', 'UNINITIALIZED']]) {
  test('accept installed package cache types ' + types.join('/'), () => {
    writeCache(types);
    const actual = assertPrefix(cache, prefix);
    for (const [index, name] of packages.entries()) {
      assert.equal(actual[name].cacheType, types[index]);
      assert.equal(actual[name].directory, fs.realpathSync(path.join(prefix, 'lib/cmake', name)));
    }
  });
}
for (const type of ['PATH', 'UNINITIALIZED']) {
  test('reject an external package for cache type ' + type, () => {
    writeCache([type, type], { SouiRuntime: foreign });
    assert.throws(() => assertPrefix(cache, prefix), /outside isolated installation: SouiRuntime/);
  });
}
test('reject missing, duplicate or unsupported package cache entries', () => {
  fs.writeFileSync(cache, 'SouiRuntime_DIR:PATH=' + path.join(prefix, 'lib/cmake/SouiRuntime') + '\n');
  assert.throws(() => assertPrefix(cache, prefix), /missing or unsupported: SouiDesktop/);
  writeCache(['STRING', 'PATH']);
  assert.throws(() => assertPrefix(cache, prefix), /missing or unsupported: SouiRuntime/);
  writeCache(['STRING', 'PATH'], { SouiRuntime: 'ignored_DIR:PATH=' + path.join(prefix, 'lib/cmake/SouiRuntime') });
  assert.throws(() => assertPrefix(cache, prefix), /missing or unsupported: SouiRuntime/);
  writeCache(['PATH', 'PATH']);
  fs.appendFileSync(cache, 'SouiRuntime_DIR:UNINITIALIZED=' + foreign + '\n');
  assert.throws(() => assertPrefix(cache, prefix), /missing or unsupported: SouiRuntime/);
});

const passed = ['runtime-primitives-test', 'runtime-installed-test'].map(name =>
  ({ name, tool: 'ctest', exitCode: 0, signal: null, errorCode: null }));
test('retain two actual Runtime passes when a later template stage fails', () => {
  assert.equal(runtimeTestsPassed([...passed, { name: 'template-link', exitCode: 1 }], true), true);
  assert.equal(runtimeTestsPassed(passed, false), false);
});
test('do not credit missing, failed, abnormal or duplicate Runtime runs', () => {
  assert.equal(runtimeTestsPassed(passed.slice(0, 1), true), false);
  for (const override of [{ exitCode: 1 }, { exitCode: null }, { exitCode: '0' },
    { signal: 'SIGTERM' }, { errorCode: 'UNKNOWN' }, { tool: 'cmake' }]) {
    assert.equal(runtimeTestsPassed([passed[0], { ...passed[1], ...override }], true), false);
  }
  assert.equal(runtimeTestsPassed([...passed, passed[0]], true), false);
});
