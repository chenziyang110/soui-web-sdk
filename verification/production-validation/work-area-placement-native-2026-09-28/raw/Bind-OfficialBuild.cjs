'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const repo = 'C:/SOUI-SDK-development/work-area-placement';
const identity = require(path.join(repo, 'tools/internal/delivery-identity.cjs'));
const evidence = require(path.join(repo, 'tools/internal/build-evidence.cjs'));
const [phase, attempt, architecture] = process.argv.slice(2);
assert(['before', 'after'].includes(phase));
assert(/^[a-z0-9-]+$/.test(attempt || ''));
assert(['x86', 'x64'].includes(architecture));

const source = identity.source(repo);
const result = {phase, attempt, architecture, at: new Date().toISOString(),
  source, guiExecuted: false, releaseReady: false};
if (phase === 'after') {
  const before = identity.readJson(path.join(__dirname, `${attempt}-before-official-input.json`));
  assert.equal(source.sha256, before.source.sha256, 'Source changed during build');
  const command = identity.readJson(path.join(__dirname, 'command-logs', `${attempt}-official-build`, 'result.json'));
  assert.equal(command.exitCode, 0);
  assert.equal(command.timedOut, false);
  assert.equal(command.interruptedByExternal, null);
  assert.equal(command.gateError, null);
  assert.equal(command.error, null);
  const build = evidence.verifyBuild(repo, architecture, 'Release');
  assert.equal(build.version, '0.3.0-preview.15');
  assert.equal(build.source.sha256, source.sha256);
  result.buildId = build.buildId;
  result.payloads = {};
  for (const target of ['SouiStarter', 'SouiWorkbench']) {
    const payload = build.payloads[target];
    assert(payload);
    identity.verifyPayload(path.join(repo, 'out', architecture, 'bin', 'Release', target), target, payload);
    result.payloads[target] = {sha256: payload.sha256, files: payload.files.length, bytes: payload.bytes};
  }
}
fs.writeFileSync(path.join(__dirname, `${attempt}-${phase}-official-input.json`),
  identity.encoded(result), {flag: 'wx'});
console.log(JSON.stringify({phase, attempt, architecture, sourceSha256: source.sha256,
  files: source.files.length, buildId: result.buildId, payloads: result.payloads}));
