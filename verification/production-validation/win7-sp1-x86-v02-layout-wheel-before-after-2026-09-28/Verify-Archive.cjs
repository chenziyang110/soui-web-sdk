'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');

const root = __dirname;
const repo = path.resolve(root, '../../..');
const relative = path.relative(repo, root).replaceAll('\\', '/');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = file => fs.readFileSync(path.join(root, file));
const json = file => JSON.parse(read(file).toString('utf8').replace(/^\uFEFF/, ''));
const manifest = json('raw-manifest.json');
const review = json('review.json');
assert.equal(manifest.schema, 'soui.win7-v02-layout-wheel-raw/v2');
assert.equal(manifest.fileCount, 157);
assert.equal(manifest.files.length, manifest.fileCount);
assert.equal(manifest.completeFrozenInputReconstructible, false);
assert.equal(manifest.nativeWheelInputsSent, 12);
assert.equal(manifest.visualPositionChanged, true);
assert.equal(manifest.win7CurrentV02Verified, false);
assert.equal(manifest.releaseReady, false);
assert.equal(review.schema, 'soui.win7-v02-layout-wheel-review/v2');
assert.equal(review.freezeSha256, manifest.freezeSha256);
assert.equal(review.serialSha256, manifest.serialSha256);
assert.equal(review.nativeWheelInputsSent, 12);
assert.equal(review.releaseReady, false);

const seen = new Set();
for (const item of manifest.files) {
  assert.match(item.path, /^raw\/[A-Za-z0-9._/-]+$/);
  assert(!item.path.split('/').includes('..'));
  assert(!seen.has(item.path), `Duplicate path ${item.path}`);
  seen.add(item.path);
  const original = fs.readFileSync(item.source);
  const worktree = read(item.path);
  const staged = cp.execFileSync('git', ['-C', repo, 'show', `:${relative}/${item.path}`],
    {windowsHide: true, maxBuffer: Math.max(1024 * 1024, item.bytes + 1024)});
  for (const [place, bytes] of [['original', original], ['worktree', worktree], ['index', staged]]) {
    assert.equal(bytes.length, item.bytes, `${place} size ${item.path}`);
    assert.equal(sha(bytes), item.sha256, `${place} hash ${item.path}`);
  }
}

const input = json('raw/frozen-delta/input.json');
const freeze = json('raw/frozen-delta/freeze.json');
assert.equal(input.schema, 'soui.win7-v02-current-layout-explore-input/v2');
assert.equal(freeze.schema, 'soui.win7-v02-current-layout-explore-freeze/v2');
assert.equal(input.sourceSha256, manifest.sourceSha256);
assert.equal(input.buildId, manifest.buildId);
assert.equal(sha(read('raw/frozen-delta/freeze.json')), manifest.freezeSha256);
assert.equal(sha(read('raw/frozen-delta/input.json')), freeze.inputSha256);
assert.equal(sha(Buffer.from(JSON.stringify(freeze.files, null, 2) + '\n')), freeze.filesSha256);
assert.equal(freeze.files.length, 13);
const prior = path.join(repo, manifest.priorFrozenArchive);
const older = path.join(repo,
  'verification/production-validation/win7-sp1-x86-v02-display-v2-interrupted-2026-09-28/raw/frozen-input');
const externalTransfer = 'C:/SOUI-SDK-production-validation/preview15-v02-win7-current-20260927/layout-explore-v2-attempt1/volume/soui-input';
const missing = [];
for (const file of freeze.files) {
  assert(!file.path.split('/').includes('..'));
  const candidates = [path.join(root, 'raw/frozen-delta', file.path),
    path.join(prior, 'raw/frozen-delta', file.path), path.join(older, file.path)];
  let source = candidates.find(candidate => fs.existsSync(candidate));
  if (!source) {
    const name = path.basename(file.path);
    assert(['SouiStarter.zip', 'SouiWorkbench.zip'].includes(name));
    source = path.join(externalTransfer, name);
    missing.push({path: name, bytes: file.bytes, sha256: file.sha256});
  }
  const bytes = fs.readFileSync(source);
  assert.equal(bytes.length, file.bytes, file.path);
  assert.equal(sha(bytes), file.sha256, file.path);
}
assert.deepEqual(missing, manifest.unarchivedPayloads);
assert.equal(missing.length, 2);
assert.equal(sha(read('raw/guest-attempt/serial-export.txt')), manifest.serialSha256);
const visual = json('raw/guest-attempt/visual-review-v2.json');
assert.equal(visual.visualPositionChanged, true);
assert.equal(visual.crossClockLatencyOrExactInputFrameAttributionClaimed, false);
assert.equal(visual.win7CurrentV02Verified, false);
for (const key of ['preWheel', 'nearWheelStart', 'afterWheel'])
  assert.equal(sha(read(`raw/guest-attempt/${visual[key].file}`)), visual[key].sha256);
const result = {
  schema: 'soui.win7-v02-layout-wheel-archive-review/v2',
  fileCount: manifest.fileCount, originalWorktreeIndexMatch: true,
  completeFrozenInputReconstructible: false, externalPayloadBytesChecked: true,
  unarchivedPayloads: missing, sourceSha256: manifest.sourceSha256,
  buildId: manifest.buildId, freezeSha256: manifest.freezeSha256,
  serialSha256: manifest.serialSha256, nativeWheelInputsSent: 12,
  visualPositionChanged: true, crossClockLatencyClaimed: false,
  imeCompositionVerified: false, resizeVerified: false, taskParallelVerified: false,
  win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(path.join(root, 'archive-review.json'), JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify(result));
