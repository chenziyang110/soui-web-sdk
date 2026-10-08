'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = path.resolve(__dirname, '../../..');
const prefix = 'verification/production-validation/win7-sp1-x86-v02-window-visibility-v10-2026-09-29/';
const manifestBytes = fs.readFileSync(path.join(__dirname, 'raw-manifest.json'));
const manifest = JSON.parse(manifestBytes);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const fromIndex = relative => execFileSync('git', ['show', `:${prefix}${relative}`],
  {cwd: root, maxBuffer: 16 * 1024 * 1024});

assert.equal(manifest.schema, 'soui.win7-v02-window-visibility-v10-raw/v1');
assert.equal(manifest.fileCount, manifest.files.length);
assert.equal(sha(fromIndex('raw-manifest.json')), sha(manifestBytes));
let originalCount = 0;
for (const entry of manifest.files) {
  assert(entry.path.startsWith('raw/') && !entry.path.includes('..'));
  const worktree = fs.readFileSync(path.join(__dirname, entry.path));
  const indexed = fromIndex(entry.path);
  assert.equal(worktree.length, entry.bytes, entry.path);
  assert.equal(indexed.length, entry.bytes, entry.path);
  assert.equal(sha(worktree), entry.sha256, entry.path);
  assert.equal(sha(indexed), entry.sha256, entry.path);
  if (fs.existsSync(entry.source)) {
    assert.equal(sha(fs.readFileSync(entry.source)), entry.sha256, entry.source);
    originalCount += 1;
  }
}
assert.equal(originalCount, manifest.files.length, 'All original files must remain available on this host');
console.log(JSON.stringify({rawFiles: manifest.files.length, originals: originalCount,
  worktreeAndIndexEqual: true, releaseReady: false}));
