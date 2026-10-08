'use strict';

const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const root = path.resolve(process.argv[2] || '');
if (!process.argv[2] || !fs.existsSync(path.join(root, 'source-manifest.json'))) {
  console.error('Usage: node inspect-post-build.cjs EXTRACTED_SOURCE_ROOT');
  process.exit(2);
}

const identity = require(path.join(root, 'tools/internal/delivery-identity.cjs'));
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'source-manifest.json'), 'utf8'));
const expected = new Set(manifest.files.map(file => file.path));
const actual = identity.walk(root, '', (relative, leaf) =>
  relative === 'source-manifest.json' || identity.skipped.has(leaf));
const actualSet = new Set(actual);
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const missing = manifest.files.filter(file => !actualSet.has(file.path)).map(file => file.path);
const changed = [];
for (const file of manifest.files) {
  if (!actualSet.has(file.path)) continue;
  const raw = fs.readFileSync(path.join(root, file.path));
  const canonical = raw.length === file.bytes && hash(raw) === file.sha256
    ? raw : identity.normalize(raw, file.normalization);
  if (canonical.length !== file.bytes || hash(canonical) !== file.sha256) changed.push(file.path);
}
const extra = actual.filter(file => !expected.has(file));
const report = {
  schema: 'soui.post-build-source-inspection/v1',
  manifestSha256: hash(fs.readFileSync(path.join(root, 'source-manifest.json'))),
  expectedFiles: manifest.files.length,
  missing,
  changed,
  generatedExtras: extra,
  sourceContentPreserved: missing.length === 0 && changed.length === 0,
  fullManifestVerifyPassesAfterBuild: missing.length === 0 && changed.length === 0 && extra.length === 0,
  releaseReady: false,
};
console.log(JSON.stringify(report, null, 2));
if (!report.sourceContentPreserved) process.exitCode = 1;
