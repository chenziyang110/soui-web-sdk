'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const raw = path.join(root, 'raw', 'manifest-set-mismatch');
const source = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/verify-manifest-interruption2-attempt2';
const names = ['result.json', 'stdout.log', 'stderr.log', 'external-gates.ndjson'];
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const stage = process.argv[2];
if (stage === 'archive') {
  assert.equal(fs.existsSync(raw), false, 'Refuse to replace evidence');
  fs.mkdirSync(raw);
  const files = names.map(name => {
    const original = path.join(source, name);
    const target = path.join(raw, name);
    fs.copyFileSync(original, target, fs.constants.COPYFILE_EXCL);
    const bytes = fs.readFileSync(original);
    assert.equal(sha(fs.readFileSync(target)), sha(bytes));
    return {name, bytes: bytes.length, sha256: sha(bytes), original};
  });
  fs.writeFileSync(path.join(raw, 'copy-review.json'), `${JSON.stringify({schema: 'soui.source-manifest-set-mismatch/v1', files}, null, 2)}\n`, {flag: 'wx'});
} else if (stage !== 'review' && stage !== '--check-index') {
  throw Error('Usage: node Archive-ManifestSetMismatch.cjs archive|review|--check-index');
}
const copied = JSON.parse(fs.readFileSync(path.join(raw, 'copy-review.json'), 'utf8'));
assert.equal(copied.schema, 'soui.source-manifest-set-mismatch/v1');
assert.deepEqual(copied.files.map(file => file.name).sort(), [...names].sort());
for (const item of copied.files) {
  const bytes = fs.readFileSync(path.join(raw, item.name));
  assert.equal(bytes.length, item.bytes);
  assert.equal(sha(bytes), item.sha256);
  assert.equal(sha(fs.readFileSync(item.original)), item.sha256);
}
if (stage === '--check-index') {
  const repo = path.resolve(root, '../../..');
  const prefix = path.relative(repo, raw).split(path.sep).join('/');
  for (const item of copied.files) {
    const bytes = cp.execFileSync('git', ['-C', repo, 'show', `:${prefix}/${item.name}`],
      {encoding: null, windowsHide: true, maxBuffer: item.bytes + 1024});
    assert.equal(bytes.length, item.bytes);
    assert.equal(sha(bytes), item.sha256);
  }
}
const result = JSON.parse(fs.readFileSync(path.join(raw, 'result.json'), 'utf8'));
assert.equal(result.exitCode, 1);
assert.equal(result.timedOut, false);
assert.equal(result.interruptedByExternal, null);
assert.equal(result.gateError, null);
assert.match(fs.readFileSync(path.join(raw, 'stderr.log'), 'utf8'), /^Source file set differs from manifest/);
console.log(JSON.stringify({files: copied.files.length, indexChecked: stage === '--check-index',
  verifyExitCode: result.exitCode, reason: 'staged-file-set-changed-after-create',
  sourceManifestVerified: false}));
