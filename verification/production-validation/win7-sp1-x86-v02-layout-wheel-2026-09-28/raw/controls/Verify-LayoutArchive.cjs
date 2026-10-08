'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const repo = 'C:/SOUI-SDK-development/preview14-scheduler';
const relative = 'verification/production-validation/win7-sp1-x86-v02-layout-wheel-2026-09-28';
const root = path.join(repo, relative);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const json = file => JSON.parse(fs.readFileSync(path.join(root, file), 'utf8').replace(/^\uFEFF/, ''));
const manifest = json('raw-manifest.json');
const review = json('review.json');
assert.equal(manifest.schema, 'soui.win7-v02-layout-wheel-raw/v1');
assert(manifest.fileCount > 100);
assert.equal(manifest.files.length, manifest.fileCount);
assert.equal(manifest.nativeWheelInputsSent, 8);
assert.equal(manifest.visualScrollEffectVerified, false);
assert.equal(manifest.win7CurrentV02Verified, false);
assert.equal(manifest.releaseReady, false);
assert.equal(review.schema, 'soui.win7-v02-layout-wheel-review/v1');
assert.equal(review.freezeSha256, manifest.freezeSha256);
assert.equal(review.serialSha256, manifest.serialSha256);
assert.equal(review.nativeWheelInputsSent, 8);
assert.equal(review.visualScrollEffectVerified, false);
assert.equal(review.releaseReady, false);
const seen = new Set();
for (const item of manifest.files) {
  assert.match(item.path, /^raw\/[A-Za-z0-9._/-]+$/);
  assert(!item.path.split('/').includes('..'));
  assert(!seen.has(item.path), `Duplicate ${item.path}`);
  seen.add(item.path);
  const original = fs.readFileSync(item.source);
  const worktree = fs.readFileSync(path.join(root, item.path));
  const staged = cp.execFileSync('git', ['-C', repo, 'show', `:${relative}/${item.path}`],
    {windowsHide: true, maxBuffer: Math.max(1024 * 1024, item.bytes + 1024)});
  for (const [place, bytes] of [['original', original], ['worktree', worktree], ['index', staged]]) {
    assert.equal(bytes.length, item.bytes, `${place} bytes ${item.path}`);
    assert.equal(sha(bytes), item.sha256, `${place} hash ${item.path}`);
  }
}
const input = json('raw/frozen-delta/input.json');
const freeze = json('raw/frozen-delta/freeze.json');
assert.equal(input.schema, 'soui.win7-v02-current-layout-explore-input/v1');
assert.equal(freeze.schema, 'soui.win7-v02-current-layout-explore-freeze/v1');
assert.equal(input.sourceSha256, manifest.sourceSha256);
assert.equal(input.buildId, manifest.buildId);
assert.equal(sha(fs.readFileSync(path.join(root, 'raw/frozen-delta/freeze.json'))), manifest.freezeSha256);
assert.equal(sha(fs.readFileSync(path.join(root, 'raw/frozen-delta/input.json'))), freeze.inputSha256);
assert.equal(sha(Buffer.from(JSON.stringify(freeze.files, null, 2) + '\n')), freeze.filesSha256);
assert.equal(input.serialFrameLimit, 64);
const expectedPrior = 'verification/production-validation/win7-sp1-x86-v02-display-v2-interrupted-2026-09-28/raw/frozen-input';
assert.equal(manifest.priorFrozenArchive, expectedPrior);
const prior = path.join(repo, expectedPrior);
const priorInput = JSON.parse(fs.readFileSync(path.join(prior, 'input.json'), 'utf8'));
const oldFiles = new Map(priorInput.transferFiles.map(file => [file.path, file]));
const changed = new Set(['export-serial.ps1', 'run-display-explore.ps1']);
const excluded = [];
assert.equal(freeze.files.length, 13);
for (const entry of freeze.files) {
  assert(!entry.path.split('/').includes('..'));
  let actual;
  if (entry.path === 'input.json' || entry.path === 'volume/soui-input/transfer-files.tsv' ||
      [...changed].some(name => entry.path === `volume/soui-input/${name}`)) {
    actual = fs.readFileSync(path.join(root, 'raw/frozen-delta', entry.path));
  } else {
    assert(entry.path.startsWith('volume/soui-input/'));
    const file = entry.path.slice('volume/soui-input/'.length);
    const previous = oldFiles.get(file);
    const current = input.transferFiles.find(item => item.path === file);
    assert(previous && current);
    assert.equal(previous.sha256, current.sha256, `Unexpected transfer drift ${file}`);
    const oldSource = path.join(prior, entry.path);
    if (fs.existsSync(oldSource)) actual = fs.readFileSync(oldSource);
    else {
      assert(['SouiStarter.zip', 'SouiWorkbench.zip'].includes(file),
        `Unexpected missing prior byte source ${file}`);
      assert.equal(entry.bytes, previous.bytes);
      assert.equal(entry.sha256, previous.sha256);
      const original = path.join('C:/SOUI-SDK-production-validation/preview15-v02-win7-current-20260927',
        'layout-explore-v1-attempt1/volume/soui-input', file);
      actual = fs.readFileSync(original);
      excluded.push({path: file, bytes: entry.bytes, sha256: entry.sha256});
    }
  }
  assert.equal(actual.length, entry.bytes, entry.path);
  assert.equal(sha(actual), entry.sha256, entry.path);
}
assert.deepEqual(excluded, manifest.unarchivedPayloads);
assert.equal(manifest.completeFrozenInputReconstructible, false);
const visual = json('raw/guest-attempt/visual-review.json');
assert.equal(visual.visualScrollEffectVerified, false);
for (const frame of visual.frames)
  assert.equal(sha(fs.readFileSync(path.join(root, 'raw/guest-attempt', frame.file))), frame.sha256);
assert.equal(sha(fs.readFileSync(path.join(root, 'raw/guest-attempt/serial-export.txt'))), manifest.serialSha256);
const result = {schema: 'soui.win7-v02-layout-wheel-archive-review/v1',
  fileCount: manifest.fileCount, originalWorktreeIndexMatch: true,
  completeFrozenInputReconstructible: false,
  unarchivedPayloads: excluded, externalPayloadBytesChecked: true,
  sourceSha256: manifest.sourceSha256, buildId: manifest.buildId,
  freezeSha256: manifest.freezeSha256, serialSha256: manifest.serialSha256,
  nativeWheelInputsSent: 8, visualScrollEffectVerified: false,
  imeCompositionVerified: false, taskParallelVerified: false,
  win7CurrentV02Verified: false, releaseReady: false};
fs.writeFileSync(path.join(root, 'archive-review.json'), JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(result));
