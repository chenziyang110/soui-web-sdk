'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const cp = require('node:child_process');

const root = __dirname;
const original = path.join(root, 'serial-export.txt');
const work = path.join(os.tmpdir(), `soui-preview8-task-serial-selftest-${crypto.randomUUID()}`);
fs.mkdirSync(work);
const raw = fs.readFileSync(original, 'ascii').trimEnd().split(/\r?\n/);
function verify(serial, expectedStatus, expectedText) {
  const result = cp.spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass',
    '-File', path.join(root, 'Verify-PublicTaskSerial.ps1'), '-InputPath', serial],
  {cwd: root, encoding: 'utf8', windowsHide: true});
  const combined = `${result.stdout}\n${result.stderr}`;
  if ((result.status === 0) !== (expectedStatus === 0) || !combined.includes(expectedText))
    throw new Error(`Unexpected verifier result ${result.status}: ${combined}`);
}
function forge(name, bytes, file) {
  const lines = [...raw];
  const index = lines.findIndex(line => line.startsWith(`FILE|${name}|`));
  if (index < 1 || index + 1 >= lines.length) throw new Error(`Missing ${name}`);
  const hash = crypto.createHash('sha256').update(bytes).digest('hex').toUpperCase();
  lines[index] = `FILE|${name}|${bytes.length}|${hash}`;
  lines[index + 1] = bytes.toString('base64');
  const output = path.join(work, file);
  fs.writeFileSync(output, `${lines.join('\n')}\n`, {flag: 'wx'});
  return output;
}
verify(original, 0, 'Verified preview.8 public task on Win7 SP1 x86');
const badEngine = forge('engine-sha256.txt', Buffer.from(`${'00 '.repeat(31)}00\r\n`, 'ascii'), 'bad-engine.txt');
verify(badEngine, 1, 'Unexpected guest SHA-256 in engine-sha256.txt');
const eventIndex = raw.findIndex(line => line.startsWith('FILE|bridge-events.ndjson|'));
const events = Buffer.from(raw[eventIndex + 1], 'base64').toString('utf8').trimEnd().split('\n');
events.pop();
const badEvents = forge('bridge-events.ndjson', Buffer.from(`${events.join('\n')}\n`, 'utf8'), 'bad-events.txt');
verify(badEvents, 1, 'Missing run boundaries');
verify(original, 0, 'Verified preview.8 public task on Win7 SP1 x86');
process.stdout.write(JSON.stringify({status: 'self-test-passed',
  badCases: ['forged engine hash rejected', 'truncated event stream with valid serial SHA-256 rejected'],
  releaseReady: false}) + '\n');
