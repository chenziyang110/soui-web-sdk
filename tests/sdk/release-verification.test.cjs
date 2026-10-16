'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const verifier = require('../../tools/internal/release-verification.cjs');
const sha = 'a'.repeat(64);
// Synthetic receipts exercise the pure logical validator. They are not actual Job evidence.
function step(name, version, index = 0, architecture = 'x64') {
  return {name, architecture, sdkVersion: version, sdkManifestSha256: sha, businessSha256: sha,
    build: {schema: 'soui.source-build/v2', version, architecture, configuration: 'Release',
      sdkOnly: false, buildId: `${name}-${index}`, source: {sha256: sha}, nativeBuild: 'passed'},
    smoke: {schema: 'soui.native-smoke/v2', version, architecture, configuration: 'Release',
      buildId: `${name}-${index}`, sourceSha256: sha, passed: true, finishedAt: '2026-10-03T00:00:00Z',
      ownedSupervision: {schema: 'soui.native-smoke-owned/v1'},
      runs: [{target: 'ReleaseUpgradeCheck', scenario: 'default', passed: true, exitCode: 0,
        timedOut: false, log: 'application-ready\nhost-close-complete\n'}]}};
}
function aggregate(architecture = 'x64') {
  const version = '0.3.0-preview.10', previous = '0.3.0-preview.9';
  const common = {sdkVersion: version, rootSourceSha256: sha, sdkManifestSha256: sha,
    passed: true, architecture, configuration: 'Release', finishedAt: '2026-10-08T00:00:00Z'};
  const relocated = step('relocation', version, 0, architecture);
  const reports = [
    {...common, schema: 'soui.generated-app-validation/v3', upgradedFrom: previous, upgradeTested: true,
      rollbackTested: true, generatedOutsideDistribution: true,
      steps: ['baseline', 'upgrade', 'rollback', 'reupgrade'].map((name, i) => step(name, i % 2 ? version : previous, i, architecture))},
    {...common, schema: 'soui.fresh-app-validation/v3', step: step('fresh', version, 0, architecture)},
    {...common, schema: 'soui.relocation/v3', step: relocated, copiedOutsideDistribution: true, buildAndNativeSmoke: true},
    {...relocated.smoke, ...common}];
  return {reports, expected: {version, sourceSha256: sha, sdkManifestSha256: sha, architecture}};
}

test('successful exit cannot replace ready/closed, identity or timeout checks', () => {
  const valid = step('upgrade', '0.3.0-preview.10');
  assert.doesNotThrow(() => verifier.validateStep(valid, valid.sdkVersion));
  for (const alter of [s => s.smoke.runs[0].log = 'process-start',
    s => s.smoke.runs[0].timedOut = true, s => s.smoke.runs[0].exitCode = 1,
    s => s.smoke.sourceSha256 = 'b'.repeat(64), s => s.smoke.passed = false]) {
    const invalid = structuredClone(valid); alter(invalid);
    assert.throws(() => verifier.validateStep(invalid, valid.sdkVersion));
  }
  assert.throws(() => verifier.validateStep(valid, '0.3.0-preview.9'));
});
test('upgrade acceptance requires all four actual phases and preserved business files', () => {
  const versions = ['0.3.0-preview.9', '0.3.0-preview.10', '0.3.0-preview.9', '0.3.0-preview.10'];
  const names = ['baseline', 'upgrade', 'rollback', 'reupgrade'];
  const steps = names.map((name, i) => step(name, versions[i], i));
  assert.doesNotThrow(() => verifier.validateUpgrade(steps, versions[0], versions[1]));
  assert.throws(() => verifier.validateUpgrade(steps.slice(0, 3), versions[0], versions[1]));
  const altered = structuredClone(steps); altered[2].businessSha256 = 'b'.repeat(64);
  assert.throws(() => verifier.validateUpgrade(altered, versions[0], versions[1]));
  const reused = structuredClone(steps); reused[3].build.buildId = reused[1].build.buildId;
  reused[3].smoke.buildId = reused[1].build.buildId;
  assert.throws(() => verifier.validateUpgrade(reused, versions[0], versions[1]));
});
test('verification workspaces reject SDK nesting, existing paths and linked ancestors', t => {
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-release-path-'));
  t.after(() => fs.rmSync(temp, {recursive: true, force: true}));
  const root = path.join(temp, 'sdk'); fs.mkdirSync(root);
  assert.doesNotThrow(() => verifier.externalWorkspace(root, path.join(temp, 'new')));
  assert.throws(() => verifier.externalWorkspace(root, root));
  assert.throws(() => verifier.externalWorkspace(root, path.join(root, 'child')));
  assert.throws(() => verifier.externalWorkspace(root, temp));
  const linked = path.join(temp, 'linked'); fs.symlinkSync(root, linked, 'junction');
  assert.throws(() => verifier.externalWorkspace(root, path.join(linked, 'new')));
});
test('business identity ignores build caches and detects source or metadata changes', t => {
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-release-business-'));
  t.after(() => fs.rmSync(temp, {recursive: true, force: true}));
  fs.mkdirSync(path.join(temp, 'app')); fs.writeFileSync(path.join(temp, 'app/main.cpp'), 'business');
  fs.writeFileSync(path.join(temp, 'project.json'), '{}');
  const before = verifier.businessSnapshot(temp).sha256;
  fs.mkdirSync(path.join(temp, 'app/out')); fs.writeFileSync(path.join(temp, 'app/out/cache'), 'cache');
  assert.equal(verifier.businessSnapshot(temp).sha256, before);
  fs.writeFileSync(path.join(temp, 'app/main.cpp'), 'changed');
  assert.notEqual(verifier.businessSnapshot(temp).sha256, before);
});

test('aggregate receipts reject stale same-version source, package and missing phases', () => {
  const {reports, expected} = aggregate();
  assert.doesNotThrow(() => verifier.validateReports(reports, expected));
  for (const alter of [r => r[0].rootSourceSha256 = 'b'.repeat(64),
    r => r[1].sdkManifestSha256 = 'b'.repeat(64), r => r[0].steps.pop(),
    r => r[0].steps[1].sdkManifestSha256 = 'b'.repeat(64), r => r[3].buildId = 'old-build',
    r => r[2].step.businessSha256 = 'b'.repeat(64), r => r[0].rollbackTested = false]) {
    const invalid = structuredClone(reports); alter(invalid);
    assert.throws(() => verifier.validateReports(invalid, expected));
  }
});
test('x86 uses explicit step and upgrade identity, never the x64 default or a mixed phase', () => {
  const version = '0.3.0-preview.10', previous = '0.3.0-preview.9';
  const valid = step('upgrade', version, 0, 'x86');
  assert.doesNotThrow(() => verifier.validateStep(valid, version, 'x86'));
  assert.throws(() => verifier.validateStep(valid, version));
  assert.throws(() => verifier.validateStep(valid, version, 'Win32'));
  for (const alter of [s => s.architecture = 'x64', s => s.build.architecture = 'x64',
    s => s.smoke.architecture = 'x64']) {
    const invalid = structuredClone(valid); alter(invalid);
    assert.throws(() => verifier.validateStep(invalid, version, 'x86'));
  }
  const phases = ['baseline', 'upgrade', 'rollback', 'reupgrade']
    .map((name, i) => step(name, i % 2 ? version : previous, i, 'x86'));
  assert.doesNotThrow(() => verifier.validateUpgrade(phases, previous, version, 'x86'));
  const mixed = structuredClone(phases); mixed[2] = step('rollback', previous, 2, 'x64');
  assert.throws(() => verifier.validateUpgrade(mixed, previous, version, 'x86'));
});

test('current release steps reject absent Owned records and unsupported profile reuse', () => {
  const version = '0.3.0-preview.10', valid = step('upgrade', version);
  assert.doesNotThrow(() => verifier.validateStep(valid, version));
  for (const alter of [s => delete s.architecture, s => delete s.smoke.ownedSupervision,
    s => s.smoke.ownedSupervision.schema = 'soui.native-smoke-owned/v0',
    s => s.build.nativeToolchain = {profile: 'LLVM22MSVC142'}]) {
    const invalid = structuredClone(valid); alter(invalid);
    assert.throws(() => verifier.validateStep(invalid, version));
  }
});

test('aggregate v3 accepts explicit x86 and rejects cross-architecture, legacy v2 and absent Owned', () => {
  const {reports, expected} = aggregate('x86');
  assert.doesNotThrow(() => verifier.validateReports(reports, expected));
  assert.throws(() => verifier.validateReports(reports, {...expected, architecture: 'x64'}));
  const legacy = structuredClone(reports);
  legacy[0].schema = 'soui.generated-app-validation/v2';
  legacy[1].schema = 'soui.fresh-app-validation/v2';
  legacy[2].schema = 'soui.relocation/v2';
  assert.throws(() => verifier.validateReports(legacy, expected));
  for (const alter of [r => r[1].architecture = 'x64',
    r => r[0].steps[1].smoke.architecture = 'x64',
    r => delete r[1].step.smoke.ownedSupervision]) {
    const invalid = structuredClone(reports); alter(invalid);
    assert.throws(() => verifier.validateReports(invalid, expected));
  }
});
