'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const raw = path.join(root, 'raw', 'manifest-refresh');
const commandRoot = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs';
const attempts = [
  {name: 'source-manifest-interruption2-attempt2', kind: 'create', code: 0, interrupted: false},
  {name: 'verify-manifest-interruption2-attempt1', kind: 'verify', code: 1, interrupted: true},
];
const names = ['result.json', 'stdout.log', 'stderr.log', 'external-gates.ndjson'];
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const stage = process.argv[2];
if (stage === 'archive') {
  assert.equal(fs.existsSync(raw), false, 'Refuse to replace evidence');
  fs.mkdirSync(raw);
  const files = [];
  for (const attempt of attempts) {
    fs.mkdirSync(path.join(raw, attempt.kind));
    for (const name of names) {
      const original = path.join(commandRoot, attempt.name, name);
      const target = path.join(raw, attempt.kind, name);
      fs.copyFileSync(original, target, fs.constants.COPYFILE_EXCL);
      const bytes = fs.readFileSync(original);
      assert.equal(sha(fs.readFileSync(target)), sha(bytes));
      files.push({path: `${attempt.kind}/${name}`, bytes: bytes.length, sha256: sha(bytes), original});
    }
  }
  fs.writeFileSync(path.join(raw, 'copy-review.json'), `${JSON.stringify({schema: 'soui.source-manifest-refresh/v1', files}, null, 2)}\n`, {flag: 'wx'});
} else if (stage !== 'review' && stage !== '--check-index') {
  throw Error('Usage: node Archive-ManifestRefresh.cjs archive|review|--check-index');
}
const copied = JSON.parse(fs.readFileSync(path.join(raw, 'copy-review.json'), 'utf8'));
assert.equal(copied.schema, 'soui.source-manifest-refresh/v1');
assert.equal(copied.files.length, attempts.length * names.length);
for (const item of copied.files) {
  assert.match(item.path, /^(?:create|verify)\/[a-zA-Z0-9._-]+$/);
  const bytes = fs.readFileSync(path.join(raw, item.path));
  assert.equal(bytes.length, item.bytes);
  assert.equal(sha(bytes), item.sha256);
  assert.equal(sha(fs.readFileSync(item.original)), item.sha256);
}
if (stage === '--check-index') {
  const repo = path.resolve(root, '../../..');
  const prefix = path.relative(repo, raw).split(path.sep).join('/');
  for (const item of copied.files) {
    const bytes = cp.execFileSync('git', ['-C', repo, 'show', `:${prefix}/${item.path}`],
      {encoding: null, windowsHide: true, maxBuffer: item.bytes + 1024});
    assert.equal(bytes.length, item.bytes);
    assert.equal(sha(bytes), item.sha256);
  }
}
for (const attempt of attempts) {
  const result = JSON.parse(fs.readFileSync(path.join(raw, attempt.kind, 'result.json'), 'utf8'));
  assert.equal(result.exitCode, attempt.code);
  assert.equal(result.timedOut, false);
  assert.equal(Boolean(result.interruptedByExternal), attempt.interrupted);
  assert.equal(result.error, null);
  if (attempt.interrupted)
    assert(result.interruptedByExternal.some(process => /^qemu-system/.test(process.Name)));
}
assert.match(fs.readFileSync(path.join(raw, 'create', 'stdout.log'), 'utf8'), /"files": 36840/);
assert.equal(fs.statSync(path.join(raw, 'verify', 'stdout.log')).size, 0);
console.log(JSON.stringify({files: copied.files.length, indexChecked: stage === '--check-index',
  createExitCode: 0, generatedFileCount: 36840, verifyExitCode: 1,
  verifyInterruptedByExternal: true, sourceManifestVerified: false}));
