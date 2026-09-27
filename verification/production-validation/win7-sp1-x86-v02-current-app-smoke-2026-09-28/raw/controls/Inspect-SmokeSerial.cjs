'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const root = __dirname;
const attempt = path.join(root, 'guest-app-smoke-attempt1');
const decoded = path.join(attempt, 'decoded');
assert.equal(fs.existsSync(decoded), false, 'Refuse to replace serial decode');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const serial = fs.readFileSync(path.join(attempt, 'serial-export.txt'));
const value = serial.toString('ascii');
assert.equal(Buffer.from(value, 'ascii').equals(serial), true);
const lines = value.split(/\r?\n/);
assert.equal(lines.shift(), 'SOUI_SERIAL_V1');
assert.equal(lines.pop(), '');
assert.equal(lines.pop(), 'SOUI_SERIAL_END');
assert.equal(lines.length % 2, 0);
fs.mkdirSync(decoded);
const entries = [];
const seen = new Set();
for (let i = 0; i < lines.length; i += 2) {
  const header = /^FILE\|([a-z0-9][a-z0-9._-]*)\|([0-9]+)\|([A-F0-9]{64})$/.exec(lines[i]);
  assert(header, `Bad header at ${i}`);
  const name = header[1];
  assert.equal(seen.has(name), false);
  seen.add(name);
  const encoded = lines[i + 1];
  assert.match(encoded, /^[A-Za-z0-9+/]*={0,2}$/);
  const bytes = Buffer.from(encoded, 'base64');
  assert.equal(bytes.toString('base64'), encoded);
  assert.equal(bytes.length, Number(header[2]));
  assert.equal(sha(bytes).toUpperCase(), header[3]);
  fs.writeFileSync(path.join(decoded, name), bytes, {flag: 'wx'});
  entries.push({name, bytes: bytes.length, sha256: sha(bytes)});
}
const summary = {schema: 'soui.win7-current-smoke-serial-inspection/v1',
  serialBytes: serial.length, serialSha256: sha(serial), frameCount: entries.length,
  entries, applicationPass: null, win7CurrentV02Verified: false, releaseReady: false};
fs.writeFileSync(path.join(attempt, 'serial-inspection.json'), JSON.stringify(summary, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({serialBytes: summary.serialBytes, serialSha256: summary.serialSha256,
  frameCount: entries.length, files: entries.map(x => x.name)}));
