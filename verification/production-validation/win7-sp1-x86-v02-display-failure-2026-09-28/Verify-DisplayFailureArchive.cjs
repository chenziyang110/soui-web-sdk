'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const relative = 'verification/production-validation/win7-sp1-x86-v02-display-failure-2026-09-28';
const root = path.join(repo, relative);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'raw-manifest.json'), 'utf8'));
assert.equal(manifest.fileCount, 66);
assert.equal(manifest.releaseReady, false);
for (const item of manifest.files) {
  const original = fs.readFileSync(item.source);
  const worktree = fs.readFileSync(path.join(root, item.path));
  const staged = cp.execFileSync('git', ['-C', repo, 'show', `:${relative}/${item.path}`],
    {windowsHide: true, maxBuffer: Math.max(1024 * 1024, item.bytes + 1024)});
  for (const [place, bytes] of [['original', original], ['worktree', worktree], ['index', staged]]) {
    assert.equal(bytes.length, item.bytes, `${place} bytes ${item.path}`);
    assert.equal(sha(bytes), item.sha256, `${place} hash ${item.path}`);
  }
}
const review = {schema: 'soui.win7-v02-display-failure-archive-review/v1',
  fileCount: manifest.fileCount, originalWorktreeIndexMatch: true,
  displayMode1024x768Verified: true, applicationWindowVerified: false,
  win7CurrentV02Verified: false, releaseReady: false};
fs.writeFileSync(path.join(root, 'archive-review.json'), JSON.stringify(review, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(review));
