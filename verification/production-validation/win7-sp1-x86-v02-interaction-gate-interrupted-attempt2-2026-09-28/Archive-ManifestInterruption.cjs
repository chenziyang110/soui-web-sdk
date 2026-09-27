'use strict';

const assert = require('node:assert/strict');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const raw = path.join(root, 'raw', 'manifest-interrupted');
const source = 'C:/SOUI-SDK-production-validation/preview15-v02-interaction-20260927/command-logs/source-manifest-interruption2-attempt1';
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
  fs.writeFileSync(path.join(raw, 'copy-review.json'), `${JSON.stringify({schema: 'soui.source-manifest-interruption/v1', files}, null, 2)}\n`, {flag: 'wx'});
} else if (stage !== 'review' && stage !== '--check-index') {
  throw Error('Usage: node Archive-ManifestInterruption.cjs archive|review|--check-index');
}

const review = JSON.parse(fs.readFileSync(path.join(raw, 'copy-review.json'), 'utf8'));
assert.equal(review.schema, 'soui.source-manifest-interruption/v1');
assert.deepEqual(review.files.map(file => file.name).sort(), [...names].sort());
for (const file of review.files) {
  const bytes = fs.readFileSync(path.join(raw, file.name));
  assert.equal(bytes.length, file.bytes);
  assert.equal(sha(bytes), file.sha256);
  assert.equal(sha(fs.readFileSync(file.original)), file.sha256);
}
if (stage === '--check-index') {
  const repo = path.resolve(root, '../../..');
  const prefix = path.relative(repo, raw).split(path.sep).join('/');
  for (const file of review.files) {
    const bytes = cp.execFileSync('git', ['-C', repo, 'show', `:${prefix}/${file.name}`],
      {encoding: null, windowsHide: true, maxBuffer: file.bytes + 1024});
    assert.equal(bytes.length, file.bytes);
    assert.equal(sha(bytes), file.sha256);
  }
}
const result = JSON.parse(fs.readFileSync(path.join(raw, 'result.json'), 'utf8'));
assert.equal(result.exitCode, 1);
assert.equal(result.timedOut, false);
assert.equal(result.cleanupReason, 'external-workload');
assert(result.interruptedByExternal.some(process => /^qemu-system/.test(process.Name)));
assert.equal(fs.statSync(path.join(raw, 'stdout.log')).size, 0);
assert.equal(fs.statSync(path.join(raw, 'stderr.log')).size, 0);
console.log(JSON.stringify({files: review.files.length, indexChecked: stage === '--check-index',
  exitCode: result.exitCode, interruptedByExternal: true, sourceManifestVerified: false}));
