'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = __dirname;
const external = 'C:\\SOUI-SDK-production-validation\\work-area-placement-20260928';
const repo = execFileSync('git', ['rev-parse', '--show-toplevel'], {
  cwd: root, encoding: 'utf8', windowsHide: true,
}).trim();
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const controls = ['Review-V11.cjs', 'Test-Review-V11-TerminalGate.cjs',
  'V11-PREPARATION.md'];
const attempts = ['v11-coverage-syntax-attempt1',
  'v11-coverage-tests-attempt1', 'v11-coverage-tests-attempt2'];
const rawFiles = attempts.flatMap(attempt =>
  ['stdout.log', 'stderr.log', 'result.json'].map(file => ({
    name: `${attempt}-${file}`,
    externalPath: path.join(external, 'command-logs', attempt, file),
  })));
for (const item of [
  ...controls.map(name => ({name, externalPath: path.join(external, name)})),
  ...rawFiles,
]) {
  const bytes = fs.readFileSync(path.join(root, item.name));
  assert.deepEqual(bytes, fs.readFileSync(item.externalPath),
    `${item.name} external original`);
  const relative = path.relative(repo, path.join(root, item.name)).replaceAll('\\', '/');
  assert.deepEqual(bytes, execFileSync('git', ['show', `:${relative}`], {
    cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024,
  }), `${item.name} Git index`);
}
const status = attempt => JSON.parse(fs.readFileSync(path.join(root,
  `${attempt}-result.json`), 'utf8'));
assert.equal(status(attempts[0]).exitCode, 0);
assert.equal(status(attempts[1]).exitCode, 1);
assert.equal(status(attempts[2]).exitCode, 0);
for (const attempt of attempts) {
  assert.equal(status(attempt).timedOut, false);
  assert.equal(status(attempt).error, null);
}
const first = fs.readFileSync(path.join(root,
  'v11-coverage-tests-attempt1-stdout.log'), 'utf8');
const second = fs.readFileSync(path.join(root,
  'v11-coverage-tests-attempt2-stdout.log'), 'utf8');
assert.match(first, /pass 7/);
assert.match(first, /fail 1/);
assert.match(second, /pass 8/);
assert.match(second, /fail 0/);
assert.equal(fs.statSync(path.join(root,
  'v11-coverage-tests-attempt2-stderr.log')).size, 0);
assert.equal(sha(fs.readFileSync(path.join(root, 'Review-V11.cjs'))),
  '1c1dea6ce63837e58264e99ffe1cad5895e14691ee6277f4261acffceb0d015d');
console.log(JSON.stringify({controls: controls.length, rawFiles: rawFiles.length,
  syntaxExit: 0, firstTestExit: 1, finalTestExit: 0, finalPass: 8,
  externalMatchesWorking: true, indexMatchesWorking: true,
  guestExecuted: false, benchmarkValid: false, releaseReady: false}));
