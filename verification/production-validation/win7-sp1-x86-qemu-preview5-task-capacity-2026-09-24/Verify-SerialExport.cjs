'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');

const root = __dirname;
const expected = ['os-version.txt', 'test-exe-sha256.txt', 'test-exitcode.txt', 'test-stderr.log', 'test-stdout.log'];
const exeSha256 = '21e61266b21ff51ac506e288f5bba8b711e57c915982ab23735f27d503cae07e';
const serial = fs.readFileSync(path.join(root, 'serial-export.txt'));
const environment = JSON.parse(fs.readFileSync(path.join(root, 'environment.json'), 'utf8'));
if (environment.releaseReady !== false || environment.build.testExeSha256 !== exeSha256 ||
    environment.result.serialSha256 !== crypto.createHash('sha256').update(serial).digest('hex') ||
    environment.result.serialFiles !== expected.length || environment.result.exitCode !== 0 ||
    environment.result.passed !== true) throw new Error('Environment and serial identities differ');
const lines = serial.toString('utf8').split(/\r?\n/);
if (lines.at(-1) === '') lines.pop();
if (lines[0] !== 'SOUI_SERIAL_V1' || lines.at(-1) !== 'SOUI_SERIAL_END' || lines.length !== 2 + 2 * expected.length)
  throw new Error('Incomplete or unexpected serial frame');

const files = new Map();
for (let index = 0; index < expected.length; index++) {
  const [tag, name, length, sha256, ...extra] = lines[1 + index * 2].split('|');
  if (tag !== 'FILE' || name !== expected[index] || extra.length || !/^[0-9]+$/.test(length) || !/^[a-f0-9]{64}$/i.test(sha256))
    throw new Error(`Invalid serial file header at ${index}`);
  const base64 = lines[2 + index * 2];
  const bytes = Buffer.from(base64, 'base64');
  if (bytes.toString('base64') !== base64 || bytes.length !== Number(length) ||
      crypto.createHash('sha256').update(bytes).digest('hex') !== sha256.toLowerCase())
    throw new Error(`Invalid serial file payload: ${name}`);
  files.set(name, bytes);
}

const os = files.get('os-version.txt');
for (const value of ['Caption=Microsoft Windows 7 Enterprise', 'OSArchitecture=32-bit',
  'ServicePackMajorVersion=1', 'Version=6.1.7601']) {
  if (!os.includes(Buffer.from(value, 'utf16le'))) throw new Error(`Unexpected guest OS: ${value}`);
}
const certutil = files.get('test-exe-sha256.txt').toString('latin1');
const hexBytes = certutil.match(/[a-f0-9]{2}(?:\s+[a-f0-9]{2}){31}/i);
if (!hexBytes || hexBytes[0].replace(/\s/g, '').toLowerCase() !== exeSha256)
  throw new Error('Guest executable SHA-256 differs from the frozen x86 build');
if (files.get('test-exitcode.txt').toString('ascii').trim() !== '0' || files.get('test-stderr.log').length !== 0 ||
    files.get('test-stdout.log').toString('utf8').trim() !==
      'Native task store capacity: boundary, cancellation, parallel admission, and scanner rejection passed')
  throw new Error('Native capacity test did not pass');

const resultDir = path.join(root, 'results');
fs.mkdirSync(resultDir, {recursive: true});
for (const [name, bytes] of files) {
  const output = path.join(resultDir, name);
  if (fs.existsSync(output)) {
    if (!fs.readFileSync(output).equals(bytes)) throw new Error(`Extracted result differs: ${name}`);
  } else fs.writeFileSync(output, bytes, {flag: 'wx'});
}
console.log(`Verified ${files.size} serial files, Win7 SP1 x86 identity, x86 executable SHA-256, and native capacity test exit 0`);
