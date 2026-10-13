'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const {validateModernSmoke} = require('../../tools/internal/build-evidence.cjs');
const {assertBuildUnchanged} = require('../../tools/internal/modern-smoke.cjs');
const payload = {sha256: 'not-an-actual-payload', files: []};
const build = {version: '0.5.0-preview.1', buildId: 'not-an-actual-build',
  source: {sha256: 'not-an-actual-source'}, architecture: 'x64', configuration: 'Release',
  optionalLibraries: {webview2: {sha256: 'not-an-actual-library'}}};
// Deliberately inadequate records. These never stand for an actual passed run.
const textOnly = {schema: 'soui.modern-smoke/1', version: build.version, buildId: build.buildId,
  sourceSha256: build.source.sha256, architecture: 'x64', configuration: 'Release', target: 'App',
  payload, pid: 1234, passed: true, exitCode: 0, timedOut: false, finishedAt: '2026-10-08T00:00:00Z',
  releaseReady: false, log: 'process-start\nhost-started;runtime=154\napplication-ready\nhost-close-complete\n'};
const v2WithoutOwned = {...textOnly, schema: 'soui.modern-smoke/v2', creationFileTime: '134000000000000000'};
const options = {root: 'F:/not-an-actual-modern-fixture'};
test('legacy Modern PID-only success is always rejected', () => {
  for (const change of [{}, {creationFileTime: '134000000000000000'},
    {ownedSupervision: {applicationNaturalClosure: true, closureConfirmed: true}}])
    assert.throws(() => validateModernSmoke(build, {...textOnly, ...change}, 'App', payload, options),
      /legacy PID-only receipts are not accepted/);
});
test('Modern v2 requires a native identity and current selected build', () => {
  for (const change of [{creationFileTime: undefined}, {creationFileTime: 134000000000000000},
    {creationFileTime: '0'}, {creationFileTime: '18446744073709551616'}, {creationFileTime: '-1'}, {creationFileTime: '1.0'}, {pid: 0},
    {buildId: 'old'}, {version: 'old'}, {sourceSha256: 'old'}, {architecture: 'x86'},
    {configuration: 'Debug'}, {target: 'Other'}, {payload: {sha256: 'changed'}},
    {passed: false}, {exitCode: 1}, {timedOut: true}, {finishedAt: ''}])
    assert.throws(() => validateModernSmoke(build, {...v2WithoutOwned, ...change}, 'App', payload, options),
      /native PID\/FILETIME/);
  assert.throws(() => validateModernSmoke({...build, optionalLibraries: {}}, v2WithoutOwned, 'App', payload, options),
    /native PID\/FILETIME/);
  assert.throws(() => validateModernSmoke(build, v2WithoutOwned, 'App', payload), /native PID\/FILETIME/);
});
test('Modern v2 textual lifecycle or declared closure cannot replace actual Owned raw evidence', () => {
  for (const ownedSupervision of [undefined, {}, {applicationNaturalClosure: true},
    {schema: 'soui.modern-smoke-owned/v1', passed: true, closureConfirmed: true,
      terminal: {rootExitCode: 0, activeProcesses: 0, terminatedOwnedJob: false}},
    {schema: 'soui.modern-smoke-owned/v1', attempt: 'outside-evidence',
      toolsRoot: options.root, controllerClosure: 'external-caller-evidence-required', noninterferenceObservation: null}])
    assert.throws(() => validateModernSmoke(build, {...v2WithoutOwned, ownedSupervision}, 'App', payload, options),
      /Invalid Modern supervision scope/);
});
// Positive v2 proof requires a genuine fresh run and archived Native/context/log
// files. Do not construct synthetic genuine-success receipts in this unit slice.

// Pure comparison failures only: no files, prepared tools or actual run proof.
test('same source and payload with a refreshed Build epoch must fail before success', () => {
  const before = {...build, optionalPayloads: {App: payload}};
  const pin = {path: 'F:/not-an-actual-build.json', requestedPath: 'F:/not-an-actual-build.json',
    resolvedPath: 'F:/not-an-actual-build.json', bytes: 500, sha256: '0'.repeat(64)};
  for (const current of [
    {...before, buildId: 'a-different-build-with-identical-source-and-payload'},
    {...before, version: 'a-different-version'},
    {...before, source: {sha256: 'changed-source'}},
    {...before, nativeToolchain: {profile: 'changed-profile'}},
    {...before, optionalPayloads: {App: {sha256: 'changed-payload', files: []}}},
    {...before, extraReceiptField: 'changed-raw-identity'}
  ]) assert.throws(() => assertBuildUnchanged(before, current, pin, pin), /Build changed during smoke/);
  for (const currentPin of [{...pin, sha256: '1'.repeat(64)}, {...pin, bytes: 501},
    {...pin, requestedPath: 'F:/another-build.json'}])
    assert.throws(() => assertBuildUnchanged(before, before, pin, currentPin), /Build changed during smoke/);
});
