'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const {validateModernSmoke} = require('../../tools/internal/build-evidence.cjs');
test('modern evidence binds optional backend, source, payload, process and complete lifecycle', () => {
  const payload = {sha256: 'payload', files: []};
  const build = {version: '0.4.0-preview.1', buildId: 'build', source: {sha256: 'source'},
    architecture: 'x64', configuration: 'Release', optionalLibraries: {webview2: {sha256: 'library'}}};
  const good = {schema: 'soui.modern-smoke/1', version: build.version, buildId: build.buildId,
    sourceSha256: build.source.sha256, architecture: 'x64', configuration: 'Release', target: 'App',
    payload, pid: 1234, passed: true, exitCode: 0, timedOut: false, finishedAt: '2026-10-08T00:00:00Z',
    log: 'host-started;runtime=154\napplication-ready\nhost-close-complete\n'};
  validateModernSmoke(build, good, 'App', payload);
  for (const change of [{buildId: 'old'}, {version: 'old'}, {sourceSha256: 'old'}, {architecture: 'x86'},
    {configuration: 'Debug'}, {target: 'Other'}, {payload: {sha256: 'changed'}}, {pid: 0},
    {passed: false}, {exitCode: 1}, {timedOut: true}, {finishedAt: ''}, {log: 'application-ready'}])
    assert.throws(() => validateModernSmoke(build, {...good, ...change}, 'App', payload), /incomplete/);
  assert.throws(() => validateModernSmoke({...build, optionalLibraries: {}}, good, 'App', payload), /incomplete/);
});
