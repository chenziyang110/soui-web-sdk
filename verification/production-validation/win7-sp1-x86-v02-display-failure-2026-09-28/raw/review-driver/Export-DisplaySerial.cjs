'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const name = process.argv[2];
assert.match(name || '', /^guest-display-explore-attempt[0-9]+$/);
const attempt = path.join(__dirname, name);
const output = path.join(attempt, 'decoded');
assert.equal(fs.existsSync(output), false, 'Refuse overwrite decoded frames');
const serial = fs.readFileSync(path.join(attempt, 'serial-export.txt'));
const lines = serial.toString('ascii').split(/\r?\n/);
assert.equal(lines.shift(), 'SOUI_SERIAL_V1');
assert.equal(lines.pop(), '');
assert.equal(lines.pop(), 'SOUI_SERIAL_END');
assert.equal(lines.length % 2, 0);
fs.mkdirSync(output);
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex').toUpperCase();
const entries = [];
for (let index = 0; index < lines.length; index += 2) {
  const header = /^FILE\|([a-z0-9][a-z0-9._-]*)\|([0-9]+)\|([A-F0-9]{64})$/.exec(lines[index]);
  assert(header, `Invalid frame header ${index}`);
  const encoded = lines[index + 1];
  const bytes = Buffer.from(encoded, 'base64');
  assert.equal(bytes.toString('base64'), encoded);
  assert.equal(bytes.length, Number(header[2]));
  assert.equal(sha(bytes), header[3]);
  fs.writeFileSync(path.join(output, header[1]), bytes, {flag: 'wx'});
  entries.push({name: header[1], bytes: bytes.length, sha256: sha(bytes).toLowerCase()});
}
assert.equal(entries.length, 24);
console.log(JSON.stringify({status: 'exported', frames: entries.length, serialBytes: serial.length,
  output, entries}));
