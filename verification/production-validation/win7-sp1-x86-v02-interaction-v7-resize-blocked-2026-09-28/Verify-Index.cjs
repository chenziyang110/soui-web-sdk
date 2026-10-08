'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const {execFileSync} = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const repo = path.resolve(__dirname, '../../..');
const manifest = JSON.parse(fs.readFileSync(path.join(__dirname, 'raw-manifest.json'), 'utf8'));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
assert.equal(manifest.files.length, 42);
for (const item of manifest.files) {
  const relative = path.relative(repo, path.join(__dirname, item.path)).replaceAll('\\', '/');
  assert(!relative.startsWith('..'));
  const indexed = execFileSync('git', ['show', `:${relative}`],
    {cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024});
  const working = fs.readFileSync(path.join(__dirname, item.path));
  assert(indexed.equals(working), `Git index differs: ${item.path}`);
  assert.equal(sha(indexed), item.sha256, item.path);
}
console.log(JSON.stringify({status: 'index-matches-original-archive', files: manifest.files.length,
  win7CurrentV02Verified: false, releaseReady: false}));
