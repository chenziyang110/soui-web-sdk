'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const cp = require('node:child_process');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const repo = path.resolve(root, '../../..');
const external = process.argv[2] ? path.resolve(process.argv[2]) : null;
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const read = file => fs.readFileSync(path.join(root, file));
const json = file => JSON.parse(read(file));
const cssHash = sha(fs.readFileSync(path.join(repo, 'demos/workbench/web/apps/react/styles.css')));
const builds = {
  x64: json('raw/build/x64-build-Release.json'),
  x86: json('raw/build/x86-build-Release.json'),
};
const artifacts = [
  ['raw/build/x64-build-Release.json', 'out/x64/build-Release.json'],
  ['raw/build/x86-build-Release.json', 'out/x86/build-Release.json'],
];

for (const [arch, name, originalDir] of [
  ['x64', 'x64-visual', 'visible-x64-attempt2'],
  ['x86', 'x86-visual', 'visible-x86-attempt1'],
]) {
  const build = builds[arch];
  const visual = json(`raw/${name}/result.json`);
  const screenshot = `raw/${name}/window.png`;
  const app = build.payloads.SouiWorkbench.files.find(file => file.path === 'SouiWorkbench.exe');
  assert.equal(build.nativeBuild, 'passed');
  assert.equal(build.architecture, arch);
  assert.equal(build.configuration, 'Release');
  assert.equal(visual.buildId, build.buildId);
  assert.equal(visual.sourceSha256, build.source.sha256);
  assert.equal(visual.cssSha256, cssHash);
  assert.equal(visual.exeSha256Before, app.sha256);
  assert.equal(visual.exeSha256After, app.sha256);
  assert.equal(visual.screenshotSha256, sha(read(screenshot)));
  assert.equal(visual.exitCode, 0);
  assert.equal(visual.failure, '');
  assert.equal(visual.timedOut, false);
  assert.equal(visual.closePosted, true);
  assert(visual.hwnd > 0 && visual.pid > 0);
  assert.equal(Array.isArray(visual.windowRect), true);
  const gates = read(`raw/${name}/gates.ndjson`).toString('utf8').trim().split('\n').map(JSON.parse);
  assert(gates.some(gate => gate.stage === 'after-close'));
  assert(gates.every(gate => Array.isArray(gate.processes) && gate.processes.length === 0));
  for (const file of ['result.json', 'gates.ndjson', 'window.png']) {
    artifacts.push([`raw/${name}/${file}`, `${originalDir}/${file}`]);
  }
}
assert.equal(builds.x64.source.sha256, builds.x86.source.sha256);
const refused = json('raw/x64-foreground-refusal/result.json');
assert.equal(refused.failure, 'Natural foreground belongs to another process');
assert.equal(refused.exitCode, 0);
assert.equal(refused.screenshotSha256, '');
for (const file of ['result.json', 'gates.ndjson']) {
  artifacts.push([`raw/x64-foreground-refusal/${file}`, `visible-x64-attempt1/${file}`]);
}
if (external) {
  for (const [archive, original] of artifacts) {
    const actual = read(archive);
    const originalPath = original.startsWith('out/') ? path.join(repo, original) : path.join(external, original);
    assert.deepEqual(actual, fs.readFileSync(originalPath), `Archived bytes differ: ${archive}`);
  }
}
for (const [archive] of artifacts) {
  const repoPath = path.relative(repo, path.join(root, archive)).replace(/\\/g, '/');
  const indexed = cp.execFileSync('git', ['-C', repo, 'show', `:${repoPath}`],
    {windowsHide:true, maxBuffer:1024 * 1024});
  assert.deepEqual(read(archive), indexed, `Git index bytes differ: ${archive}`);
}
console.log(JSON.stringify({verified:true,architectures:['x64','x86'],screenshots:2,
  sourceSha256:builds.x64.source.sha256,archivedFiles:artifacts.length,
  externalCompared:Boolean(external),indexCompared:true}));
