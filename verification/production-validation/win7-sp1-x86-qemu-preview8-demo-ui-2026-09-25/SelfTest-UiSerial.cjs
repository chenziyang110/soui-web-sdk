'use strict';
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const crypto = require('node:crypto');

const root = __dirname;
const original = fs.readFileSync(path.join(root, 'serial-export.txt'), 'ascii');
const lines = original.trimEnd().split(/\r?\n/);
const scratch = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-demo-ui-serial-'));
const sample = path.join(scratch, 'sample.txt');
const verifier = path.join(root, 'Verify-UiSerial.ps1');
const results = path.join(root, 'results');
function snapshotResults() {
  const snapshot = new Map();
  function visit(directory) {
    for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
      const full = path.join(directory, entry.name);
      if (entry.isDirectory()) visit(full);
      else snapshot.set(path.relative(results, full), fs.readFileSync(full));
    }
  }
  visit(results);
  return snapshot;
}
const archived = snapshotResults();
function assertArchiveUnchanged() {
  const current = snapshotResults();
  if (current.size !== archived.size) throw new Error('Archived result file count changed');
  for (const [name, bytes] of archived) {
    if (!current.has(name) || !bytes.equals(current.get(name)))
      throw new Error(`Archived result bytes changed: ${name}`);
  }
}
function replaceFrame(name, transform) {
  const changed = [...lines];
  const index = changed.findIndex(line => line.startsWith(`FILE|${name}|`));
  if (index < 0) throw new Error(`Missing fixture frame: ${name}`);
  const bytes = Buffer.from(transform(Buffer.from(changed[index + 1], 'base64').toString('ascii')), 'ascii');
  const hash = crypto.createHash('sha256').update(bytes).digest('hex').toUpperCase();
  changed[index] = `FILE|${name}|${bytes.length}|${hash}`;
  changed[index + 1] = bytes.toString('base64');
  return changed.join('\n') + '\n';
}
function check(name, changed, expectedError) {
  assertArchiveUnchanged();
  fs.writeFileSync(sample, changed, 'ascii');
  const run = cp.spawnSync('powershell.exe',
    ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', verifier, '-InputPath', sample],
    {encoding: 'utf8', windowsHide: true});
  assertArchiveUnchanged();
  if (run.error || run.status === null)
    throw new Error(`${name}: verifier could not run: ${run.error || run.signal}`);
  const expectedSuccess = !expectedError;
  if ((run.status === 0) !== expectedSuccess)
    throw new Error(`${name}: unexpected verifier exit ${run.status}: ${run.stdout} ${run.stderr}`);
  if (expectedError && !`${run.stdout}\n${run.stderr}`.includes(expectedError))
    throw new Error(`${name}: wrong rejection reason: ${run.stdout} ${run.stderr}`);
  console.log(`${name}: ${expectedSuccess ? 'accepted' : 'rejected'}; archived results unchanged`);
}
try {
  const corrupted = [...lines];
  corrupted[2] = (corrupted[2][0] === 'A' ? 'B' : 'A') + corrupted[2].slice(1);
  check('modified payload frame', corrupted.join('\n') + '\n', 'SHA-256 mismatch');
  check('truncated serial stream', lines.slice(0, -1).join('\n') + '\n', 'Serial export framing is incomplete');
  check('nonzero app exit with valid frame hash', replaceFrame('app-exitcode.txt', () => '1\r\n'),
    'Guest operation failed: app-exitcode.txt');
  check('missing shutdown with valid frame hash', replaceFrame('app-host.log', text => text.replace('adapter-webview-destroyed\r\n', '')),
    'Workbench lifecycle step missing, repeated or out of order');
  check('out-of-order shutdown with valid frame hash', replaceFrame('app-host.log', text =>
    text.replace('adapter-bridge-shutdown\r\nadapter-events-drained', 'adapter-events-drained\r\nadapter-bridge-shutdown')),
    'Workbench lifecycle step missing, repeated or out of order');
  check('original serial stream', original);
} finally {
  if (fs.existsSync(sample)) fs.unlinkSync(sample);
  fs.rmdirSync(scratch);
}
