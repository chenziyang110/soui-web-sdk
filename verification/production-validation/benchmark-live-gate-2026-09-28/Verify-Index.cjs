'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const { execFileSync } = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const repo = path.resolve(__dirname, '../../..');
const manifests = ['raw-manifest.json', 'raw-manifest-attempt2.json',
  'raw-manifest-attempt3.json', 'raw-manifest-attempt4.json',
  'raw-manifest-attempt5.json', 'raw-manifest-attempt6.json',
  'raw-manifest-attempt7.json', 'raw-manifest-attempt8.json',
  'raw-manifest-attempt9.json', 'raw-manifest-attempt10.json',
  'raw-manifest-attempt11.json', 'raw-manifest-attempt12.json',
  'raw-manifest-attempt13.json', 'raw-manifest-attempt14.json',
  'raw-manifest-attempt15.json', 'raw-manifest-attempt16.json',
  'raw-manifest-attempt17.json', 'raw-manifest-attempt18.json'].map(name =>
  JSON.parse(fs.readFileSync(path.join(__dirname, name), 'utf8')));
assert.equal(manifests[0].files.length, 4);
assert.equal(manifests[1].files.length, 3);
assert.equal(manifests[2].files.length, 3);
assert.equal(manifests[3].files.length, 3);
assert.equal(manifests[4].files.length, 3);
assert.equal(manifests[5].files.length, 6);
assert.equal(manifests[6].files.length, 6);
assert.equal(manifests[7].files.length, 6);
assert.equal(manifests[8].files.length, 3);
assert.equal(manifests[9].files.length, 9);
assert.equal(manifests[10].files.length, 6);
assert.equal(manifests[11].files.length, 3);
assert.equal(manifests[12].files.length, 6);
assert.equal(manifests[13].files.length, 15);
assert.equal(manifests[14].files.length, 3);
assert.equal(manifests[15].files.length, 6);
assert.equal(manifests[16].files.length, 12);
assert.equal(manifests[17].files.length, 9);
for (const entry of manifests.flatMap(manifest => manifest.files)) {
  const file = path.join(__dirname, 'raw', entry.path);
  const relative = path.relative(repo, file).replaceAll('\\', '/');
  assert(!relative.startsWith('..'));
  const indexed = execFileSync('git', ['show', `:${relative}`], {
    cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024,
  });
  assert(indexed.equals(fs.readFileSync(file)), `Git index differs: ${entry.path}`);
  assert.equal(crypto.createHash('sha256').update(indexed).digest('hex'), entry.sha256);
}
process.stdout.write('Git index matches one hundred six original test files\n');
