'use strict';
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');

const root = __dirname;
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const manifest = JSON.parse(fs.readFileSync(path.join(root, 'operations-manifest.json')));
assert.equal(manifest.schema, 'soui.win7-v02-layout-operations/v1');
assert.equal(manifest.archiveExitCode, 0);
assert.equal(manifest.archiveVerificationExitCode, 0);
assert.equal(manifest.sourceManifestAttemptExitCode, 1);
assert.equal(manifest.sourceManifestInterruptedByExternal, true);
assert.equal(manifest.sourceManifestVerified, false);
assert.equal(manifest.releaseReady, false);
const expected = new Map([
  ['archive-layout-explore-v1-attempt2', ['result.json', 'stdout.log', 'stderr.log']],
  ['verify-layout-archive-v1-attempt1', ['result.json', 'stdout.log', 'stderr.log']],
  ['layout-wheel-source-manifest-attempt1',
    ['result.json', 'stdout.log', 'stderr.log', 'external-gates.ndjson']],
]);
assert.equal(manifest.files.length, 10);
const listed = new Set();
for (const item of manifest.files) {
  assert.match(item.path, /^[a-z0-9-]+\/[a-z0-9.-]+$/);
  assert(!listed.has(item.path));
  listed.add(item.path);
  const [name, file] = item.path.split('/');
  assert(expected.get(name)?.includes(file), item.path);
  const bytes = fs.readFileSync(path.join(root, 'operations', name, file));
  assert.equal(bytes.length, item.bytes, item.path);
  assert.equal(sha(bytes), item.sha256, item.path);
}
for (const [name, files] of expected) {
  assert.deepEqual(fs.readdirSync(path.join(root, 'operations', name)).sort(), files.sort());
  for (const file of files) assert(listed.has(`${name}/${file}`));
}
assert.deepEqual(fs.readdirSync(path.join(root, 'operations')).sort(), [...expected.keys()].sort());
const result = name => JSON.parse(fs.readFileSync(path.join(root, 'operations', name, 'result.json')));
for (const name of ['archive-layout-explore-v1-attempt2', 'verify-layout-archive-v1-attempt1']) {
  const record = result(name);
  assert.equal(record.exitCode, 0);
  assert.equal(record.timedOut, false);
  assert.equal(record.error, null);
}
const interrupted = result('layout-wheel-source-manifest-attempt1');
assert.equal(interrupted.exitCode, 1);
assert.equal(interrupted.timedOut, false);
assert.equal(interrupted.error, null);
assert.equal(interrupted.cleanupReason, 'external-workload');
assert(Array.isArray(interrupted.interruptedByExternal) && interrupted.interruptedByExternal.length > 0);
assert(interrupted.interruptedByExternal.every(process => /^qemu-/.test(process.Name)));
const gates = fs.readFileSync(path.join(root, 'operations',
  'layout-wheel-source-manifest-attempt1', 'external-gates.ndjson'), 'utf8')
  .trim().split('\n').map(line => JSON.parse(line));
assert(gates.some(gate => gate.processes?.some(process =>
  interrupted.interruptedByExternal.some(external => external.ProcessId === process.ProcessId))));
console.log(JSON.stringify({operationFiles: manifest.files.length, archiveExitCode: 0,
  archiveVerificationExitCode: 0, sourceManifestAttemptExitCode: 1,
  sourceManifestVerified: false, releaseReady: false}));
