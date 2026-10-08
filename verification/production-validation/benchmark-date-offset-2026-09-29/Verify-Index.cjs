'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = __dirname;
const repo = path.resolve(root, '../../..');
const external = 'C:\\SOUI-SDK-production-validation\\benchmark-date-offset-20260929';
const names = ['red', 'green', 'merged-suite'];
for (const label of names) {
  for (const suffix of ['stdout.log', 'stderr.log', 'exit.txt']) {
    const name = `${label}.${suffix}`;
    const local = fs.readFileSync(path.join(root, 'raw', name));
    assert.deepEqual(local, fs.readFileSync(path.join(external, name)),
      `${name} external original`);
    const relative = path.relative(repo, path.join(root, 'raw', name))
      .replaceAll('\\', '/');
    const staged = execFileSync('git', ['show', `:${relative}`], {
      cwd: repo, windowsHide: true, maxBuffer: 2 * 1024 * 1024,
    });
    assert.deepEqual(staged, local, `${name} Git index`);
  }
}
const read = name => fs.readFileSync(path.join(root, 'raw', name), 'utf8');
const expect = (label, exit, pass, fail) => {
  assert.equal(read(`${label}.exit.txt`), String(exit));
  assert.equal(read(`${label}.stderr.log`), '');
  const stdout = read(`${label}.stdout.log`);
  assert.match(stdout, new RegExp(`\\bpass ${pass}\\b`));
  assert.match(stdout, new RegExp(`\\bfail ${fail}\\b`));
};
expect('red', 1, 9, 1);
assert.match(read('red.stdout.log'), /Series runner creation identity is missing/);
expect('green', 0, 10, 0);
expect('merged-suite', 0, 41, 0);
console.log(JSON.stringify({rawFiles: 9, externalMatchesWorking: true,
  indexMatchesWorking: true, redExit: 1, greenExit: 0,
  suitePass: 41, suiteFail: 0, benchmarkValid: false, releaseReady: false}));
