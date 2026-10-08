'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const repo = path.resolve(root, '../../..');
const source = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/source-manifest-v6-attempt2-create-attempt1';
const target = path.join(root, 'operations', 'source-manifest-create-attempt1');
const names = ['result.json', 'stdout.log', 'stderr.log', 'external-gates.ndjson'];
const mode = process.argv[2];
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const original = name => path.join(source, name);
const archived = name => path.join(target, name);

if (mode === 'archive') {
  assert.equal(fs.existsSync(target), false, 'Refuse to replace gate original');
  fs.mkdirSync(target, {recursive: true});
  const items = names.map(name => {
    const bytes = fs.readFileSync(original(name));
    fs.copyFileSync(original(name), archived(name), fs.constants.COPYFILE_EXCL);
    assert.equal(sha(fs.readFileSync(archived(name))), sha(bytes));
    return {name, bytes: bytes.length, sha256: sha(bytes)};
  });
  fs.writeFileSync(archived('copy-review.json'),
    JSON.stringify({schema: 'soui.source-manifest-interrupted-gate/v1', items}, null, 2) + '\n',
    {flag: 'wx'});
} else if (mode !== 'review' && mode !== '--check-index') {
  throw Error('Usage: node Review-ManifestGate.cjs archive|review|--check-index');
}

const record = JSON.parse(fs.readFileSync(archived('copy-review.json'), 'utf8'));
assert.equal(record.schema, 'soui.source-manifest-interrupted-gate/v1');
assert.deepEqual(record.items.map(item => item.name), names);
for (const item of record.items) {
  for (const file of [original(item.name), archived(item.name)]) {
    const bytes = fs.readFileSync(file);
    assert.equal(bytes.length, item.bytes, file);
    assert.equal(sha(bytes), item.sha256, file);
  }
  if (mode === '--check-index') {
    const relative = path.relative(repo, archived(item.name)).split(path.sep).join('/');
    const bytes = cp.execFileSync('git', ['-C', repo, 'show', `:${relative}`],
      {encoding: null, windowsHide: true, maxBuffer: item.bytes + 1024});
    assert.equal(sha(bytes), item.sha256, `index: ${item.name}`);
  }
}
const driver = JSON.parse(fs.readFileSync(archived('result.json'), 'utf8'));
assert.equal(driver.exitCode, 1);
assert.equal(driver.timedOut, false);
assert.equal(driver.gateError, null);
assert.equal(driver.error, null);
assert.equal(driver.cleanupReason, 'external-workload');
assert.equal(driver.interruptedByExternal.length, 1);
assert.equal(driver.interruptedByExternal[0].ProcessId, 37072);
assert.equal(driver.interruptedByExternal[0].Name, 'qemu-system-x86_64.exe');
assert.equal(fs.statSync(archived('stdout.log')).size, 0);
assert.equal(fs.statSync(archived('stderr.log')).size, 0);
const oldManifest = cp.execFileSync('git', ['-C', repo, 'show',
  '9ea126870f64303dee5bd5aea9d24cb2c52113d9:source-manifest.json'],
  {encoding: null, windowsHide: true, maxBuffer: 64 * 1024 * 1024});
assert.equal(sha(oldManifest),
  'dcb28875d2954233db4159d67c0673d37b313248808d7da1e85f6b7374dee0bb');
// The first review ran before the later successful manifest refresh and checked
// current bytes against HEAD. That transient assertion cannot be repeated afterward.
console.log(JSON.stringify({files: record.items.length,
  indexChecked: mode === '--check-index', driverExitCode: driver.exitCode,
  externalQemuPid: 37072, manifestCreateCompleted: false,
  releaseReady: false}));
