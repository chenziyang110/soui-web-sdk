'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = __dirname;
const original = 'C:\\SOUI-SDK-production-validation\\work-area-placement-20260928';
const repo = execFileSync('git', ['rev-parse', '--show-toplevel'], {
  cwd: root, encoding: 'utf8', windowsHide: true,
}).trim();
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const controls = ['Launch-V11.ps1', 'Run-V11.ps1', 'Review-V11.cjs',
  'Test-Review-V11-TerminalGate.cjs', 'V11-PREPARATION.md'];
const raw = ['review-test.stdout.txt', 'review-test.stderr.txt',
  'review-test.exit.txt', 'powershell-parser.stdout.txt',
  'powershell-parser.exit.txt', 'node-check.stdout.txt',
  'node-check.stderr.txt', 'node-check.exit.txt'];
for (const name of [...controls, ...raw]) {
  const bytes = fs.readFileSync(path.join(root, name));
  const external = raw.includes(name)
    ? path.join(original, 'v11-host-terminal-attempt1', name)
    : path.join(original, 'v11-host-terminal-attempt1/control-originals', name);
  assert.deepEqual(bytes, fs.readFileSync(external), `${name} external original`);
  const relative = path.relative(repo, path.join(root, name)).replaceAll('\\', '/');
  assert.deepEqual(bytes, execFileSync('git', ['show', `:${relative}`], {
    cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024,
  }), `${name} Git index`);
}
for (const name of ['review-test', 'powershell-parser', 'node-check'])
  assert.equal(fs.readFileSync(path.join(root, `${name}.exit.txt`), 'utf8'), '0');
const tests = fs.readFileSync(path.join(root, 'review-test.stdout.txt'), 'utf8');
assert.match(tests, /pass 6/);
assert.match(tests, /fail 0/);
assert.match(fs.readFileSync(path.join(root, 'powershell-parser.stdout.txt'), 'utf8'),
  /Files=2\r?\nErrors=0/);
assert.equal(fs.statSync(path.join(root, 'review-test.stderr.txt')).size, 0);
assert.equal(fs.statSync(path.join(root, 'node-check.stderr.txt')).size, 0);
assert.equal(sha(fs.readFileSync(path.join(root, 'Launch-V11.ps1'))),
  'f3eed8598f68a49d22f9d231ef353e0af9cc81c2491d3cfc8300c64632fff065');
assert.equal(sha(fs.readFileSync(path.join(root, 'Review-V11.cjs'))),
  '1f93c2c7769243755521f1b9c7debbabd43303f7dd8a28f5bd71114dc8e2294a');
console.log(JSON.stringify({controls: controls.length, rawFiles: raw.length,
  parserExit: 0, syntaxExit: 0, reviewerPass: 6,
  externalMatchesWorking: true, indexMatchesWorking: true,
  guestExecuted: false, benchmarkValid: false, releaseReady: false}));
