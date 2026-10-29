'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const sdk = require('../../tools/sdk/soui-sdk.cjs');
function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-publication-diagnostics-'));
  t.after(() => fs.rmSync(root, {recursive: true, force: true}));
  const source = path.join(root, 'source'), output = path.join(root, 'output');
  for (const relative of sdk.managedRoots) {
    const file = path.join(source, relative, 'fixture.txt');
    fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, 'fixture\n');
  }
  for (const relative of sdk.managedFiles) {
    const file = path.join(source, relative);
    fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, 'notice\n');
  }
  return {root, source, output};
}
test('persistent SDK publication denial preserves the native error and final path observations', {skip: process.platform !== 'win32'}, t => {
  const f = fixture(t), original = fs.renameSync;
  const denied = Object.assign(new Error('persistent publication denial'), {code: 'EPERM', errno: -4048, syscall: 'rename'});
  let attempts = 0;
  fs.renameSync = (from, to) => {
    if (to === f.output) { ++attempts; throw denied; }
    return original(from, to);
  };
  try {
    assert.throws(() => sdk.pack(f.source, f.output, '0.6.0'), error => {
      assert.equal(error, denied);
      assert.equal(error.code, 'EPERM');
      assert.equal(error.publication.schema, 'soui.sdk-publication-failure/v1');
      assert.equal(error.publication.attempts.length, 6);
      assert.equal(error.publication.totalRetryDelayMs, 450);
      assert.deepEqual(error.publication.attempts.map(a => a.code), Array(6).fill('EPERM'));
      assert.equal(error.publication.attempts[5].errno, -4048);
      assert.equal(error.publication.stageState.kind, 'directory');
      assert.equal(error.publication.outputState.kind, 'missing');
      assert.equal(error.publication.lockOwnerKnown, false);
      return true;
    });
  } finally { fs.renameSync = original; }
  assert.equal(attempts, 6);
  assert.equal(fs.existsSync(f.output), false);
  assert.equal(fs.readdirSync(f.root).some(n => n.startsWith('output.staging-')), false);
});
test('SDK staging cleanup failure cannot replace the first publication error', t => {
  const f = fixture(t), rename = fs.renameSync, remove = fs.rmSync;
  const original = Object.assign(new Error('original I/O failure'), {code: 'EIO', syscall: 'rename'});
  const cleanup = Object.assign(new Error('stage cleanup denied'), {code: 'EACCES', syscall: 'rm'});
  let stage;
  fs.renameSync = (from, to) => {
    if (to === f.output) { stage = from; throw original; }
    return rename(from, to);
  };
  fs.rmSync = (target, ...args) => {
    if (target === stage) throw cleanup;
    return remove(target, ...args);
  };
  try {
    assert.throws(() => sdk.pack(f.source, f.output, '0.6.0'), error => {
      assert.equal(error, original);
      assert.equal(error.publication.attempts.length, 1);
      assert.equal(error.publication.totalRetryDelayMs, 0);
      assert.equal(error.cleanupFailure.code, 'EACCES');
      assert.equal(error.cleanupFailure.stage, stage);
      return true;
    });
  } finally { fs.renameSync = rename; fs.rmSync = remove; }
  assert.equal(fs.existsSync(stage), true);
  assert.equal(fs.existsSync(f.output), false);
});
test('unreadable publication diagnostics never mask the original rename failure', t => {
  const f = fixture(t), rename = fs.renameSync, stat = fs.lstatSync;
  const original = Object.assign(new Error('original error'), {code: 'EIO'});
  let publicationFailed = false;
  fs.renameSync = (from, to) => { if (to === f.output) { publicationFailed = true; throw original; } return rename(from, to); };
  fs.lstatSync = (file, ...args) => {
    if (publicationFailed && (file === f.output || path.basename(file).startsWith('output.staging-'))) {
      throw Object.assign(new Error('observation denied'), {code: 'EACCES'});
    }
    return stat(file, ...args);
  };
  try {
    assert.throws(() => sdk.pack(f.source, f.output, '0.6.0'), error => {
      assert.equal(error, original);
      assert.equal(error.publication.stageState.kind, 'unreadable');
      assert.equal(error.publication.stageState.error.code, 'EACCES');
      return true;
    });
  } finally { fs.renameSync = rename; fs.lstatSync = stat; }
});
test('SDK CLI emits structured publication failure diagnostics and still exits nonzero', t => {
  const f = fixture(t), preload = path.join(f.root, 'denied.cjs');
  fs.writeFileSync(preload, [
    "const fs = require('node:fs'); const rename = fs.renameSync;",
    'const output = ' + JSON.stringify(f.output) + ';',
    "fs.renameSync = (from, to) => { if (to === output) throw Object.assign(new Error('original cli error'), {code:'EIO',errno:-5,syscall:'rename'}); return rename(from,to); };",
  ].join('\n'));
  const result = cp.spawnSync(process.execPath, ['--require', preload,
    path.resolve(__dirname, '../../tools/sdk/soui-sdk.cjs'), 'pack', '--source', f.source, '--output', f.output, '--version', '0.6.0'],
    {encoding: 'utf8', windowsHide: true});
  assert.equal(result.status, 1); assert.equal(result.stdout, '');
  assert.match(result.stderr, /SDK: original cli error/);
  const line = result.stderr.split(/\r?\n/).find(value => value.startsWith('SDK diagnostics: '));
  const details = JSON.parse(line.slice('SDK diagnostics: '.length));
  assert.equal(details.publication.attempts.length, 1);
  assert.equal(details.publication.attempts[0].code, 'EIO');
  assert.equal(details.publication.outputState.kind, 'missing');
  assert.equal(fs.existsSync(f.output), false);
});
