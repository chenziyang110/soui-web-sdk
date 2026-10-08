'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const cp = require('node:child_process');

const root = __dirname;
const work = path.join(os.tmpdir(), `soui-preview8-consumer-serial-selftest-${crypto.randomUUID()}`);
fs.mkdirSync(work);
for (const name of [
  'Verify-ConsumerSerial.ps1', 'input.json', 'payload-sha256.tsv', 'sdk-lock.json',
  'sdk-manifest.json', 'build-Release.json', 'run-consumer.cmd', 'verify-consumer.ps1',
  'export-serial.ps1',
]) fs.copyFileSync(path.join(root, name), path.join(work, name));

function verify(serial, expectedStatus, expectedText) {
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', path.join(work, 'Verify-ConsumerSerial.ps1'), '-InputPath', serial],
  {cwd: work, encoding: 'utf8', windowsHide: true});
  if ((result.status === 0) !== (expectedStatus === 0) ||
      !`${result.stdout}\n${result.stderr}`.includes(expectedText)) {
    throw new Error(`Unexpected verifier result ${result.status}: ${result.stdout}\n${result.stderr}`);
  }
}

const good = path.join(root, 'serial-export.txt');
verify(good, 0, 'Verified preview.8 independent consumer: 15 serial files');
const lines = fs.readFileSync(good, 'ascii').trimEnd().split(/\r?\n/);
const index = lines.findIndex(line => line.startsWith('FILE|engine-sha256.txt|'));
if (index < 1 || index + 1 >= lines.length) throw new Error('Engine hash result not found');
const forged = Buffer.from(`${'00 '.repeat(31)}00\r\n`, 'ascii');
const hash = crypto.createHash('sha256').update(forged).digest('hex').toUpperCase();
lines[index] = `FILE|engine-sha256.txt|${forged.length}|${hash}`;
lines[index + 1] = forged.toString('base64');
const bad = path.join(work, 'tampered-serial.txt');
fs.writeFileSync(bad, `${lines.join('\n')}\n`, {flag: 'wx'});
verify(bad, 1, 'Unexpected guest SHA-256 in engine-sha256.txt');
process.stdout.write(JSON.stringify({status: 'self-test-passed', badCase: 'forged engine hash rejected',
  work, releaseReady: false}) + '\n');
