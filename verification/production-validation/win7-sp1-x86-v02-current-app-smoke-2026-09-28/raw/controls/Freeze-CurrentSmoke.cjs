'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = path.join(__dirname, 'app-smoke-attempt1');
const freezePath = path.join(root, 'freeze.json');
assert.equal(fs.existsSync(freezePath), false, 'Refuse to replace freeze');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const encode = value => JSON.stringify(value, null, 2) + '\n';
const input = JSON.parse(fs.readFileSync(path.join(root, 'input.json'), 'utf8'));
assert.equal(input.schema, 'soui.win7-v02-current-app-smoke-input/v1');
function walk(base, prefix = '') {
  return fs.readdirSync(base, {withFileTypes: true}).flatMap(item => {
    assert.equal(item.isSymbolicLink(), false);
    const relative = prefix ? `${prefix}/${item.name}` : item.name;
    if (item.isDirectory()) return walk(path.join(base, item.name), relative);
    assert.equal(item.isFile(), true);
    return [relative];
  }).sort();
}
const files = walk(root).map(relative => {
  const bytes = fs.readFileSync(path.join(root, relative));
  return {path: relative, bytes: bytes.length, sha256: sha(bytes)};
});
const freeze = {
  schema: 'soui.win7-v02-current-app-smoke-freeze/v1',
  version: input.version, buildId: input.buildId, sourceSha256: input.sourceSha256,
  inputSha256: sha(fs.readFileSync(path.join(root, 'input.json'))),
  filesSha256: sha(Buffer.from(encode(files))), files,
  guestExecuted: false, win7CurrentV02Verified: false, releaseReady: false,
};
fs.writeFileSync(freezePath, encode(freeze), {flag: 'wx'});
console.log(JSON.stringify({status: 'frozen', files: files.length,
  freezeSha256: sha(fs.readFileSync(freezePath)), guestExecuted: false}));
