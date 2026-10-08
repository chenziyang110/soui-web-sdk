'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const [architecture, attempt, phase] = process.argv.slice(2);
assert(['x86', 'x64'].includes(architecture));
assert(/^[a-z0-9-]+$/.test(attempt || ''));
assert(['before', 'after'].includes(phase));
const repo = 'C:/SOUI-SDK-development/work-area-placement';
const output = path.join(__dirname, 'gui-runs', attempt);
const official = architecture === 'x64' ? 'workarea-x64-attempt2' : 'workarea-x86-attempt1';
const binding = JSON.parse(fs.readFileSync(path.join(__dirname,
  `${official}-after-official-input.json`), 'utf8'));
const identity = require(path.join(repo, 'tools/internal/delivery-identity.cjs'));
const evidence = require(path.join(repo, 'tools/internal/build-evidence.cjs'));
const build = evidence.verifyBuild(repo, architecture, 'Release');
assert.equal(build.version, '0.3.0-preview.15');
assert.equal(build.source.sha256, binding.source.sha256);
assert.equal(build.buildId, binding.buildId);
const app = path.join(repo, 'out', architecture, 'bin', 'Release',
  'SouiWorkbench', 'SouiWorkbench.exe');
const appSha256 = crypto.createHash('sha256').update(fs.readFileSync(app)).digest('hex');
const appEntry = build.payloads.SouiWorkbench.files.find(entry =>
  entry.path === 'SouiWorkbench.exe');
assert(appEntry);
assert.equal(appSha256, appEntry.sha256);
for (const target of ['SouiStarter', 'SouiWorkbench']) {
  const expected = binding.payloads[target];
  assert(expected);
  const actual = build.payloads[target];
  assert.equal(actual.sha256, expected.sha256);
  assert.equal(actual.files.length, expected.files);
  assert.equal(actual.bytes, expected.bytes);
  identity.verifyPayload(path.join(repo, 'out', architecture, 'bin', 'Release', target),
    target, actual);
}
const probe = 'C:/SOUI-SDK-production-validation/preview15-v02-win7-current-20260927/' +
  'layout-interaction-v8-attempt1/volume/soui-input/V02InputProbe.exe';
const probeSha256 = crypto.createHash('sha256').update(fs.readFileSync(probe)).digest('hex');
assert.equal(probeSha256,
  '2ad0e9eb92c7ef7b2ba8e72c672db58fd9fa250f5dbbb1698795c359cb0e5d96');
const result = {schema: 'soui.work-area-gui-identity/v1', architecture, attempt, phase,
  at: new Date().toISOString(), sourceSha256: build.source.sha256,
  buildId: build.buildId, appSha256, probeSha256,
  payloads: Object.fromEntries(['SouiStarter', 'SouiWorkbench'].map(target =>
    [target, {sha256: build.payloads[target].sha256,
      files: build.payloads[target].files.length, bytes: build.payloads[target].bytes}])),
  guiVerified: false, win7Verified: false, releaseReady: false};
if (phase === 'after') {
  const before = JSON.parse(fs.readFileSync(path.join(output, 'identity-before.json'), 'utf8'));
  for (const key of ['architecture', 'attempt', 'sourceSha256', 'buildId',
    'appSha256', 'probeSha256', 'payloads']) {
    assert.deepEqual(result[key], before[key], `${key} changed during GUI run`);
  }
}
fs.writeFileSync(path.join(output, `identity-${phase}.json`),
  JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({phase, architecture, attempt,
  sourceSha256: result.sourceSha256, buildId: result.buildId,
  appSha256, payloadSha256: result.payloads.SouiWorkbench.sha256}));
