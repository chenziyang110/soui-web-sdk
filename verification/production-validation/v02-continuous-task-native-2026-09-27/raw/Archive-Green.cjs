'use strict';
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const id = require(path.join(repo, 'tools/internal/delivery-identity.cjs'));
const dest = path.join(repo, 'verification/production-validation/v02-continuous-task-native-2026-09-27');
const read = name => id.readJson(path.join(__dirname, name));
const result = name => read(`command-logs/${name}/result.json`);
const succeeded = name => {
  const value = result(name);
  assert.equal(value.exitCode, 0, name);
  assert.equal(value.timedOut, false, name);
  assert.equal(value.error, null, name);
};
const failed = name => {
  const value = result(name);
  assert.equal(value.exitCode, 1, name);
  assert.equal(value.timedOut, false, name);
  assert.equal(value.error, null, name);
};
for (const name of ['green-x64-attempt1-configure', 'green-x64-attempt1-build',
  'green-x64-attempt2-configure', 'green-x64-attempt2-build',
  'green-x64-attempt3-configure', 'green-x64-attempt3-build',
  'green-x64-attempt3-interaction', 'green-x64-attempt3-renewal', 'green-x64-attempt3-driver',
  'green-x86-attempt1-configure', 'green-x86-attempt1-build',
  'green-x86-attempt1-interaction', 'green-x86-attempt1-renewal', 'green-x86-attempt1-driver',
  'web-attempt1-typecheck', 'web-attempt1-driver']) succeeded(name);
for (const name of ['green-x64-attempt1-driver', 'green-x64-attempt2-driver',
  'green-x64-attempt2-interaction']) failed(name);
assert.equal(read('green-x64-attempt1-interrupted-review.json').testExecuted, false);
assert.equal(read('green-x64-attempt2-failure-review.json').interactionExitCode, 1);
assert.match(fs.readFileSync(path.join(__dirname, 'command-logs/green-x64-attempt2-interaction/stderr.log'), 'utf8'),
  /Demo bootstrap rejected/);
for (const attempt of ['green-x64-attempt3', 'green-x86-attempt1']) {
  const before = read(`${attempt}-before-input.json`);
  const after = read(`${attempt}-after-input.json`);
  assert.equal(before.source.sha256, after.source.sha256);
  assert.equal(after.binaries.files.length, 2);
  for (const stage of ['before', 'compile', 'test', 'regression', 'after'])
    assert.equal(read(`${attempt}-${stage}-gate.json`).processes.length, 0);
}
const source = id.source(repo);
assert.equal(source.sha256, read('green-x64-attempt3-after-input.json').source.sha256);
assert.equal(source.sha256, read('green-x86-attempt1-after-input.json').source.sha256);
assert.equal(source.files.length, 718);
assert.equal(read('web-attempt1-gate.json').processes.length, 0);
const review = {schema: 'soui.v02-continuous-native-review/v1', at: new Date().toISOString(),
  version: '0.3.0-preview.15', sourceSha256: source.sha256, sourceFiles: source.files.length,
  x64Attempt1: 'compiled-then-interrupted-by-external-qemu-before-tests',
  x64Attempt2: 'first-test-failed-on-second-trusted-bootstrap-in-fixture',
  x64Attempt3: 'two-native-tests-exit-0', x86Attempt1: 'two-native-tests-exit-0',
  webTypecheckExitCode: 0, newApplicationBuildExecuted: false, guiExecuted: false,
  releaseReady: false,
  note: 'The test fixture now issues a valid independent grant in a second session, so the ownership assertions reach E_TASK_NOT_FOUND. No SDK Core implementation was changed.'};
fs.writeFileSync(path.join(__dirname, 'native-review.json'), id.encoded(review), {flag: 'wx'});
fs.mkdirSync(path.join(dest, 'raw'), {recursive: true});
const files = id.walk(__dirname).filter(name => name === 'native-review.json' ||
  name.startsWith('green-') || name.startsWith('web-') ||
  name.startsWith('command-logs/green-') || name.startsWith('command-logs/web-') ||
  ['Archive-Green.cjs', 'Preserve-InterruptedBuild.cjs', 'Preserve-InteractionFailure.cjs',
    'Run-Green.ps1', 'Run-Web.ps1', 'Bind-Inputs.cjs', 'Capture-Command.cjs'].includes(name));
assert(files.length > 60);
const manifest = [];
for (const name of files) {
  const original = path.join(__dirname, name);
  const relative = 'raw/' + name;
  const to = path.join(dest, relative);
  const bytes = fs.readFileSync(original);
  fs.mkdirSync(path.dirname(to), {recursive: true});
  fs.writeFileSync(to, bytes, {flag: 'wx'});
  manifest.push({path: relative, original, bytes: bytes.length, sha256: id.hash(bytes)});
}
fs.writeFileSync(path.join(dest, 'raw-manifest.json'),
  id.encoded({schema: 'soui.raw-evidence/v1', files: manifest}), {flag: 'wx'});
console.log(JSON.stringify({rawFiles: manifest.length, sourceSha256: source.sha256,
  x64: review.x64Attempt3, x86: review.x86Attempt1, webTypecheck: 0}));
