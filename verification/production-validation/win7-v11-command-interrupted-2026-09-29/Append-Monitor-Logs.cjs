'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const raw = path.join(root, 'raw');
const origin = 'C:/SOUI-SDK-production-validation/work-area-placement-20260928/guest-native-resize-v11-attempt2';
const names = [
  'monitor-run-1790658874320.log',
  'monitor-screen-1790658823840.log',
  'monitor-screen-1790658936069.log',
];
const manifestPath = path.join(root, 'raw-manifest.json');
const manifest = JSON.parse(fs.readFileSync(manifestPath, 'utf8'));
assert.equal(manifest.length, 22);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
for (const name of names) {
  const bytes = fs.readFileSync(path.join(origin, name));
  fs.writeFileSync(path.join(raw, name), bytes, {flag: 'wx'});
  manifest.push({name, bytes: bytes.length, sha256: sha(bytes)});
}
manifest.sort((a, b) => a.name.localeCompare(b.name));
fs.writeFileSync(manifestPath, JSON.stringify(manifest, null, 2) + '\n');
console.log(JSON.stringify({status: 'appended', rawFiles: manifest.length,
  manifestSha256: sha(fs.readFileSync(manifestPath))}));
