'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.resolve(__dirname, '../..');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const read = name => fs.readFileSync(path.join(root, name));
const json = name => JSON.parse(read(name).toString('utf8').replace(/^\uFEFF/, ''));
const manifest = json('raw-manifest.json');
const review = json('review.json');
const archive = json('archive-review.json');
assert.equal(manifest.schema, 'soui.win7-v02-layout-wheel-raw/v1');
assert.equal(manifest.fileCount, manifest.files.length);
assert(manifest.fileCount > 100);
assert.equal(manifest.nativeWheelInputsSent, 8);
assert.equal(manifest.visualScrollEffectVerified, false);
assert.equal(manifest.releaseReady, false);
assert.equal(review.schema, 'soui.win7-v02-layout-wheel-review/v1');
assert.equal(archive.schema, 'soui.win7-v02-layout-wheel-archive-review/v1');
assert.equal(archive.originalWorktreeIndexMatch, true);
assert.equal(archive.completeFrozenInputReconstructible, false);
assert.equal(manifest.completeFrozenInputReconstructible, false);
assert.equal(manifest.unarchivedPayloads.length, 2);
for (const field of ['sourceSha256', 'buildId', 'freezeSha256', 'serialSha256']) {
  assert.equal(review[field], manifest[field]);
  assert.equal(archive[field], manifest[field]);
}
for (const result of [review, archive]) {
  assert.equal(result.nativeWheelInputsSent, 8);
  assert.equal(result.visualScrollEffectVerified, false);
  assert.equal(result.win7CurrentV02Verified, false);
  assert.equal(result.releaseReady, false);
}
const listed = new Set();
for (const item of manifest.files) {
  assert.match(item.path, /^raw\/[A-Za-z0-9._/-]+$/);
  assert(!item.path.split('/').includes('..'));
  assert(!listed.has(item.path), `Duplicate ${item.path}`);
  listed.add(item.path);
  const bytes = read(item.path);
  assert.equal(bytes.length, item.bytes, item.path);
  assert.equal(sha(bytes), item.sha256, item.path);
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
const input = json('raw/frozen-delta/input.json');
const freeze = json('raw/frozen-delta/freeze.json');
assert.equal(sha(read('raw/frozen-delta/freeze.json')), manifest.freezeSha256);
assert.equal(sha(read('raw/frozen-delta/input.json')), freeze.inputSha256);
assert.equal(sha(Buffer.from(JSON.stringify(freeze.files, null, 2) + '\n')), freeze.filesSha256);
assert.equal(input.serialFrameLimit, 64);
assert.equal(input.sourceSha256, manifest.sourceSha256);
assert.equal(input.buildId, manifest.buildId);
const expectedPrior = 'verification/production-validation/win7-sp1-x86-v02-display-v2-interrupted-2026-09-28/raw/frozen-input';
assert.equal(manifest.priorFrozenArchive, expectedPrior);
const prior = path.join(path.dirname(root),
  'win7-sp1-x86-v02-display-v2-interrupted-2026-09-28/raw/frozen-input');
const priorInput = JSON.parse(fs.readFileSync(path.join(prior, 'input.json'), 'utf8'));
const oldFiles = new Map(priorInput.transferFiles.map(item => [item.path, item]));
assert.equal(freeze.files.length, 13);
for (const item of freeze.files) {
  assert(!item.path.split('/').includes('..'));
  let bytes;
  if (item.path === 'input.json' || item.path === 'volume/soui-input/transfer-files.tsv' ||
      ['export-serial.ps1', 'run-display-explore.ps1']
        .some(name => item.path === `volume/soui-input/${name}`)) {
    bytes = read(`raw/frozen-delta/${item.path}`);
  } else {
    assert(item.path.startsWith('volume/soui-input/'));
    const name = item.path.slice('volume/soui-input/'.length);
    const old = oldFiles.get(name);
    const current = input.transferFiles.find(file => file.path === name);
    assert(old && current);
    assert.equal(old.sha256, current.sha256, name);
    const oldSource = path.join(prior, item.path);
    if (fs.existsSync(oldSource)) bytes = fs.readFileSync(oldSource);
    else {
      assert(['SouiStarter.zip', 'SouiWorkbench.zip'].includes(name),
        `Unexpected missing prior byte source ${name}`);
      assert.equal(item.bytes, old.bytes);
      assert.equal(item.sha256, old.sha256);
      assert(manifest.unarchivedPayloads.some(payload =>
        payload.path === name && payload.bytes === item.bytes && payload.sha256 === item.sha256));
      continue;
    }
  }
  assert.equal(bytes.length, item.bytes, item.path);
  assert.equal(sha(bytes), item.sha256, item.path);
}
const serial = read('raw/guest-attempt/serial-export.txt');
assert.equal(sha(serial), manifest.serialSha256);
const lines = serial.toString('ascii').split(/\r?\n/);
assert.equal(lines.shift(), 'SOUI_SERIAL_V1');
assert.equal(lines.pop(), '');
assert.equal(lines.pop(), 'SOUI_SERIAL_END');
assert.equal(lines.length % 2, 0);
const frames = new Map();
for (let index = 0; index < lines.length; index += 2) {
  const header = /^FILE\|([a-z0-9][a-z0-9._-]*)\|([0-9]+)\|([A-F0-9]{64})$/.exec(lines[index]);
  assert(header);
  assert(!frames.has(header[1]));
  const bytes = Buffer.from(lines[index + 1], 'base64');
  assert.equal(bytes.length, Number(header[2]));
  assert.equal(sha(bytes).toUpperCase(), header[3]);
  frames.set(header[1], bytes);
}
assert.equal(frames.size, 50);
const decoded = name => frames.get(name)?.toString('utf8').replace(/^\uFEFF/, '');
assert.equal(decoded('run-exitcode.txt')?.trim(), '0');
const inspectRaw = decoded('window-inspect.json');
const inspect = JSON.parse(inspectRaw);
const creation = /"creationFiletime":([0-9]+)/.exec(inspectRaw)?.[1];
assert(creation && inspect.hwnd > 0);
for (let index = 1; index <= 8; ++index) {
  const raw = decoded(`wheel-${index}.json`);
  const wheel = JSON.parse(raw);
  assert.equal(wheel.action, 'wheel');
  assert.equal(wheel.pid, inspect.pid);
  assert.equal(wheel.hwnd, inspect.hwnd);
  assert.equal(wheel.foregroundBefore, true);
  assert.equal(/"creationFiletime":([0-9]+)/.exec(raw)?.[1], creation);
  assert.match(decoded(`wheel-${index}-process.txt`), /ExitCode=0\r?\nTimedOut=False/);
}
const visual = json('raw/guest-attempt/visual-review.json');
assert.equal(visual.visualScrollEffectVerified, false);
for (const frame of visual.frames)
  assert.equal(sha(read(`raw/guest-attempt/${frame.file}`)), frame.sha256);
console.log(JSON.stringify({rawFiles: manifest.fileCount, serialFrames: frames.size,
  completeFrozenInputReconstructible: false, unarchivedPayloads: 2, nativeWheelInputsSent: 8,
  visualScrollEffectVerified: false, win7CurrentV02Verified: false, releaseReady: false}));
