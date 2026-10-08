'use strict';
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const name = process.argv[2];
if (!/^guest-display-explore-attempt[0-9]+$/.test(name || '')) throw Error('Invalid attempt');
const attempt = path.join(__dirname, name);
const serial = fs.readFileSync(path.join(attempt, 'serial-export.txt'));
const lines = serial.toString('ascii').split(/\r?\n/);
const frames = [];
for (let i = 0; i < lines.length - 1; ++i) {
  const header = /^FILE\|([a-z0-9][a-z0-9._-]*)\|([0-9]+)\|([A-F0-9]{64})$/.exec(lines[i]);
  if (!header) continue;
  const bytes = Buffer.from(lines[++i] || '', 'base64');
  const sha = crypto.createHash('sha256').update(bytes).digest('hex').toUpperCase();
  if (bytes.length !== Number(header[2]) || sha !== header[3]) throw Error(`Corrupt frame ${header[1]}`);
  const decoded = bytes.length < 8192 && /(?:log|exitcode|complete|inspect|attempt|run|exception)/.test(header[1])
    ? (bytes[0] === 0xff && bytes[1] === 0xfe ? bytes.subarray(2).toString('utf16le') : bytes.toString('utf8'))
    : null;
  frames.push({name: header[1], bytes: bytes.length, text: decoded});
}
console.log(JSON.stringify({attempt: name, serialBytes: serial.length, hasEnd: lines.includes('SOUI_SERIAL_END'), frames}, null, 2));
