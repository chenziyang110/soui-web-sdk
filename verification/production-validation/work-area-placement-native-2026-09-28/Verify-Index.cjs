'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = path.resolve(__dirname, '../../..');
const prefix = 'verification/production-validation/work-area-placement-native-2026-09-28/';
const manifest = JSON.parse(fs.readFileSync(path.join(__dirname, 'raw-manifest.json'), 'utf8'));
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readIndex = file => execFileSync('git', ['show', `:${prefix}${file}`],
  {cwd: root, maxBuffer: 64 * 1024 * 1024});

assert.equal(sha256(readIndex('raw-manifest.json')),
  sha256(fs.readFileSync(path.join(__dirname, 'raw-manifest.json'))));
for (const entry of manifest.files) {
  assert(entry.path.startsWith('raw/') && !entry.path.includes('..'));
  const bytes = readIndex(entry.path);
  assert.equal(bytes.length, entry.bytes, entry.path);
  assert.equal(sha256(bytes), entry.sha256, entry.path);
}
console.log(`Git index matches ${manifest.files.length} archived original files`);
