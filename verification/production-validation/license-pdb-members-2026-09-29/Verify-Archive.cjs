'use strict';

const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {execFileSync} = require('node:child_process');

const root = __dirname;
const repository = execFileSync('git', ['rev-parse', '--show-toplevel'], {
  cwd: root, encoding: 'utf8', windowsHide: true,
}).trim();
const relative = name => path.relative(repository, path.join(root, name)).replaceAll('\\', '/');
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const analysis = JSON.parse(fs.readFileSync(path.join(root, 'analysis.json'), 'utf8'));
assert.equal(analysis.schema, 'soui.license-pdb-members-analysis/v1');
for (const [name, expected] of Object.entries(analysis.raw)) {
  const local = fs.readFileSync(path.join(root, name));
  assert.equal(local.length, expected.bytes, `${name} working size`);
  assert.equal(hash(local), expected.sha256, `${name} working digest`);
  const indexed = execFileSync('git', ['show', `:${relative(name)}`], {
    cwd: repository, windowsHide: true, maxBuffer: 4 * 1024 * 1024,
  });
  assert.deepEqual(indexed, local, `${name} Git index bytes`);
}
for (const name of ['x64-debug-capture.json', 'x64-dll-debug.stdout.txt',
  'x64-dll-debug.stderr.txt']) {
  const local = fs.readFileSync(path.join(root, name));
  const indexed = execFileSync('git', ['show', `:${relative(name)}`], {
    cwd: repository, windowsHide: true, maxBuffer: 4 * 1024 * 1024,
  });
  assert.deepEqual(indexed, local, `${name} Git index bytes`);
}
console.log(JSON.stringify({rawFiles: Object.keys(analysis.raw).length + 3,
  workingMatchesAnalysis: true, indexMatchesWorking: true}));
