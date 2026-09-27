'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const json = name => JSON.parse(fs.readFileSync(path.join(root, name), 'utf8'));
const manifest = json('raw-manifest.json');
const review = json('review.json');
const archive = json('archive-review.json');
assert.equal(manifest.schema, 'soui.win7-v02-visible-page-raw/v1');
assert.equal(manifest.fileCount, 112);
assert.equal(manifest.files.length, manifest.fileCount);
assert.equal(manifest.dataPageVisibleVerified, true);
assert.equal(manifest.v02RealInputVerified, false);
assert.equal(manifest.releaseReady, false);
assert.equal(review.schema, 'soui.win7-v02-visible-page-review/v1');
assert.equal(archive.schema, 'soui.win7-v02-visible-page-archive-review/v1');
assert.equal(archive.originalWorktreeIndexMatch, true);
for (const field of ['sourceSha256', 'buildId', 'freezeSha256', 'serialSha256', 'visualFrameSha256']) {
  assert.equal(review[field], manifest[field], `Review ${field}`);
  assert.equal(archive[field], manifest[field], `Archive ${field}`);
}
assert.equal(archive.fileCount, manifest.fileCount);
assert.equal(review.dataPageVisibleVerified, true);
assert.equal(review.v02RealInputVerified, false);
assert.equal(review.win7CurrentV02Verified, false);
assert.equal(review.releaseReady, false);
const listed = new Set();
for (const item of manifest.files) {
  assert.match(item.path, /^raw\/[A-Za-z0-9._/-]+$/);
  assert(!item.path.split('/').includes('..'));
  assert(!listed.has(item.path), `Duplicate ${item.path}`);
  listed.add(item.path);
  const bytes = fs.readFileSync(path.join(root, item.path));
  assert.equal(bytes.length, item.bytes, `Length ${item.path}`);
  assert.equal(sha(bytes), item.sha256, `Hash ${item.path}`);
}
function walk(directory, prefix) {
  return fs.readdirSync(directory, {withFileTypes: true}).flatMap(entry => {
    assert.equal(entry.isSymbolicLink(), false);
    const relative = `${prefix}/${entry.name}`;
    if (entry.isDirectory()) return walk(path.join(directory, entry.name), relative);
    assert.equal(entry.isFile(), true);
    return [relative];
  });
}
assert.deepEqual(walk(path.join(root, 'raw'), 'raw').sort(), [...listed].sort());
for (const [file, field] of [['raw/guest-attempt/serial-export.txt', 'serialSha256'],
  ['raw/guest-attempt/timeline-018.png', 'visualFrameSha256'],
  ['raw/frozen-identity/freeze.json', 'freezeSha256']])
  assert.equal(sha(fs.readFileSync(path.join(root, file))), manifest[field]);
const operationNames = ['archive-visible-v2-attempt1', 'review-visible-v2-archive-attempt1'];
const expectedOperations = [];
for (const name of operationNames) {
  const base = `operations/${name}`;
  const result = json(`${base}/result.json`);
  assert.equal(result.exitCode, 0, name);
  assert.equal(result.timedOut, false, name);
  assert.equal(result.error, null, name);
  for (const [key, file] of [['stdout', 'stdout.log'], ['stderr', 'stderr.log']]) {
    const bytes = fs.readFileSync(path.join(root, base, file));
    assert.equal(bytes.length, result[key].bytes, `${name} ${key} bytes`);
    assert.equal(sha(bytes), result[key].sha256, `${name} ${key} hash`);
  }
  expectedOperations.push(`${base}/result.json`, `${base}/stdout.log`, `${base}/stderr.log`);
}
assert.deepEqual(walk(path.join(root, 'operations'), 'operations').sort(), expectedOperations.sort());
console.log(JSON.stringify({rawFiles: manifest.fileCount, operationFiles: expectedOperations.length,
  dataPageVisibleVerified: true, v02RealInputVerified: false, releaseReady: false}));
