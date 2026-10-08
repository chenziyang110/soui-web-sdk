'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = path.join(__dirname, 'attempt1');
const freezeFile = path.join(root, 'freeze.json');
assert.equal(fs.existsSync(freezeFile), false, 'Refuse to replace frozen input');
const input = JSON.parse(fs.readFileSync(path.join(root, 'preflight-input.json'), 'utf8'));
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const encode = value => JSON.stringify(value, null, 2) + '\n';
function walk(relative = '') {
  return fs.readdirSync(path.join(root, relative), {withFileTypes: true}).flatMap(item => {
    assert.equal(item.isSymbolicLink(), false);
    const child = relative ? `${relative}/${item.name}` : item.name;
    if (item.isDirectory()) return walk(child);
    assert.equal(item.isFile(), true);
    return [child];
  }).sort();
}
const files = walk().map(file => {
  const bytes = fs.readFileSync(path.join(root, file));
  return {path: file, bytes: bytes.length, sha256: sha(bytes)};
});
const freeze = {
  schema: 'soui.win7-v02-current-preflight-freeze/v1',
  version: input.version, buildId: input.buildId, sourceSha256: input.sourceSha256,
  inputSha256: sha(fs.readFileSync(path.join(root, 'preflight-input.json'))),
  filesSha256: sha(Buffer.from(encode(files))), files,
  guestExecuted: false, releaseReady: false,
};
fs.writeFileSync(freezeFile, encode(freeze), {flag: 'wx'});
console.log(JSON.stringify({status: 'preflight-frozen', files: files.length,
  freezeSha256: sha(fs.readFileSync(freezeFile)), guestExecuted: false, releaseReady: false}));
