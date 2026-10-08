'use strict';

// Read-only consistency checks; never execute a captured historical fixture.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const assert = require('node:assert/strict');
const root = __dirname;
const sha = data => crypto.createHash('sha256').update(data).digest('hex');
const json = data => JSON.parse(data.toString('utf8').replace(/^\uFEFF/, ''));
const index = json(fs.readFileSync(path.join(root, 'index.json')));
const args = process.argv.slice(2);
assert(args.length === 0 || (args.length === 2 && args[0] === '--source-root'),
  'Usage: node Verify-Evidence.cjs [--source-root REPOSITORY_ROOT]');
const sourceRoot = args.length ? path.resolve(args[1]) : null;

function relativeFile(base, relative) {
  assert(typeof relative === 'string' && !path.isAbsolute(relative) &&
    !relative.includes('\\') && !relative.includes(':') &&
    relative.split('/').every(part => part && part !== '.' && part !== '..'),
  `Invalid evidence path: ${relative}`);
  const parts = relative.split('/');
  let current = base;
  for (const part of parts) {
    current = path.join(current, part);
    assert(!fs.lstatSync(current).isSymbolicLink(), `Linked evidence: ${relative}`);
  }
  assert(fs.statSync(current).isFile(), `Not a file: ${relative}`);
  return current;
}

function check(base, record) {
  const data = fs.readFileSync(relativeFile(base, record.path));
  assert.equal(data.length, record.bytes, `Byte count: ${record.path}`);
  assert.equal(sha(data), record.sha256, `Hash: ${record.path}`);
  return data;
}

function sourceMapJs(lock) {
  return Object.entries(lock.packages || {}).filter(([name]) =>
    name === 'node_modules/source-map-js' || name.endsWith('/node_modules/source-map-js'))
    .map(([name, value]) => ({path: name, version: value.version,
      dev: value.dev === true, optional: value.optional === true}));
}

assert.equal(index.schema, 'soui.maintenance-evidence/v1');
assert.equal(new Set(index.files.map(file => file.path)).size, index.files.length);
for (const file of index.files) check(root, file);
for (const source of index.sourceSnapshots) {
  check(root, source.snapshot);
  assert.equal(source.snapshot.bytes, source.originalBytes);
  assert.equal(source.snapshot.sha256, source.originalSha256);
}
const binding = index.sourceSnapshots.map(s => ({root: s.originalRoot,
  path: s.originalPath, bytes: s.originalBytes, sha256: s.originalSha256}));
assert.equal(sha(Buffer.from(JSON.stringify(binding))), index.sourceSnapshotBindingSha256);

const responses = new Map();
for (const query of index.queries) {
  assert.equal(query.method, 'GET');
  assert.equal(query.exitCode, 0);
  assert.equal(query.credentialsRecorded, false);
  assert.equal(query.args[query.args.indexOf('--method') + 1], 'GET');
  responses.set(query.id, json(check(root, query.stdout)));
  check(root, query.stderr);
}
const mainBefore = responses.get('main-before').sha;
assert.equal(mainBefore, responses.get('main-after').sha);
assert.equal(mainBefore, index.defaultBranch.commitAtStart);
assert.equal(mainBefore, index.defaultBranch.commitAtEnd);
assert.equal(responses.get('advisory').ghsa_id, index.advisory.ghsa);
assert.equal(responses.get('advisory').cve_id, index.advisory.cve);
assert.equal(index.alerts.length, 11);
assert.deepEqual(index.alerts.map(alert => alert.number),
  Array.from({length: 11}, (_, n) => n + 57));

for (const alert of index.alerts) {
  const captured = responses.get('open-alerts').find(item => item.number === alert.number);
  assert(captured, `Absent captured alert: ${alert.number}`);
  assert.equal(captured.state, alert.capturedState);
  assert.equal(captured.dependency.manifest_path, alert.path);
  assert.equal(captured.dependency.scope, alert.dependencyScope);
  assert.equal(captured.security_advisory.ghsa_id, alert.ghsa);
  assert.equal(captured.security_advisory.cve_id, alert.cve);
  assert.equal(captured.security_vulnerability.first_patched_version.identifier,
    alert.firstPatchedVersion);
  const manifestSource = index.sourceSnapshots.find(source =>
    source.scope === 'archive-manifests' && source.originalPath === alert.rawManifest.path);
  assert(manifestSource, `Absent manifest snapshot: ${alert.rawManifest.path}`);
  const manifest = json(check(root, manifestSource.snapshot));
  const entry = manifest.files.find(file =>
    file[alert.rawManifest.entryPathField] === alert.rawManifest.entryKey);
  assert(entry, `Absent archive entry: ${alert.path}`);
  assert.equal(entry.bytes, alert.bytes);
  assert.equal(entry.sha256, alert.sha256);
  assert.equal(alert.rawManifest.bytes, alert.bytes);
  assert.equal(alert.rawManifest.sha256, alert.sha256);
  assert.equal(alert.resolvedVersion, '1.2.1');
  if (sourceRoot) {
    const versions = sourceMapJs(json(check(sourceRoot, alert)));
    assert(versions.length > 0 && versions.every(value => value.version === '1.2.1'),
      `Historical version changed: ${alert.path}`);
  }
}

for (const lock of [...index.activeLocks, ...index.mainLocks]) {
  const captured = json(check(root, {path: lock.snapshot, bytes: lock.bytes,
    sha256: lock.sha256}));
  assert.deepEqual(sourceMapJs(captured), lock.sourceMapJs);
  assert(lock.sourceMapJs.every(value => value.version === '1.2.2'),
    `Captured active lock is affected: ${lock.path}`);
  if (lock.commit) assert.equal(lock.commit, mainBefore);
}

console.log(JSON.stringify({schema: 'soui.maintenance-hash-check/v1',
  evidenceConsistent: true, storedFilesVerified: index.files.length,
  sourceSnapshotsVerified: index.sourceSnapshots.length,
  historicalManifestEntriesVerified: index.alerts.length,
  liveHistoricalLocksVerified: sourceRoot ? index.alerts.length : 0,
  activeLockSnapshotsVerified: index.activeLocks.length,
  mainLockSnapshotsVerified: index.mainLocks.length,
  sourceSnapshotBindingSha256: index.sourceSnapshotBindingSha256,
  freshNpmAuditExecuted: false, historicalFixtureExecuted: false,
  nativeBuildExecuted: false, guiExecuted: false, githubMutationExecuted: false}, null, 2));
