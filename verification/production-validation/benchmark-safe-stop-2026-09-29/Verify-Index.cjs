'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = __dirname;
const original = 'C:\\SOUI-SDK-production-validation\\benchmark-safe-stop-20260929';
const repo = execFileSync('git', ['rev-parse', '--show-toplevel'], {
  cwd: root, encoding: 'utf8', windowsHide: true,
}).trim();
const expected = new Map([
  ['red-attempt1', {exit: 1, pass: 0, fail: 2}],
  ['green-attempt1', {exit: 1, pass: 21, fail: 1}],
  ['green-attempt2', {exit: 0, pass: 22, fail: 0}],
]);
let rawFiles = 0;
for (const [label, counts] of expected) {
  for (const suffix of ['stdout.txt', 'stderr.txt', 'exit.txt']) {
    const name = `${label}.${suffix}`;
    const bytes = fs.readFileSync(path.join(root, name));
    assert.deepEqual(bytes, fs.readFileSync(path.join(original, name)),
      `${name} external original`);
    const relative = path.relative(repo, path.join(root, name)).replaceAll('\\', '/');
    assert.deepEqual(bytes, execFileSync('git', ['show', `:${relative}`], {
      cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024,
    }), `${name} Git index`);
    rawFiles++;
  }
  assert.equal(Number(fs.readFileSync(path.join(root, `${label}.exit.txt`), 'utf8')),
    counts.exit);
  const stdout = fs.readFileSync(path.join(root, `${label}.stdout.txt`), 'utf8');
  assert.match(stdout, new RegExp(`pass ${counts.pass}\\b`));
  assert.match(stdout, new RegExp(`fail ${counts.fail}\\b`));
  assert.equal(fs.statSync(path.join(root, `${label}.stderr.txt`)).size, 0);
}
const one = fs.readFileSync(path.join(repo, 'benchmarks/protocol/run-one.cjs'), 'utf8');
const series = fs.readFileSync(path.join(repo, 'benchmarks/protocol/run-series.cjs'), 'utf8');
assert(!one.includes("spawnSync('taskkill.exe'"));
assert(!series.includes("spawnSync('taskkill.exe'"));
assert(one.includes('treeCleanupVerified: false'));
assert(series.includes('cleanupCompletenessUnverified: true'));
console.log(JSON.stringify({rawFiles, redExit: 1, firstGreenExit: 1,
  finalGreenExit: 0, finalPass: 22, externalMatchesWorking: true,
  indexMatchesWorking: true, hostIsolationClaim: false}));
