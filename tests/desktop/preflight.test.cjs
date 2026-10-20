'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const { spawnSync } = require('node:child_process');
const { generatePreflight } = require('../../tools/desktop-preflight.cjs');

test('generator rejects interpolated target/version content', () => {
  assert.throws(() => generatePreflight({ platform: 'linux', arch: "x64';bad" }));
  assert.throws(() => generatePreflight({ platform: 'macos', arch: 'arm64', minimumOSVersion: '12.0;bad' }));
});
test('native shell scripts parse without Node or target loader execution', { skip: process.platform === 'win32' }, () => {
  for (const platform of ['linux', 'macos']) {
    const script = generatePreflight({ platform, arch: 'x64', minimumOSVersion: '12.3.1' });
    assert.equal(script.filename, 'doctor.sh');
    assert.equal(spawnSync('/bin/sh', ['-n'], { input: script.content }).status, 0);
    assert.doesNotMatch(script.content, /\bldd\b|\bnode\b|sudo|LD_LIBRARY_PATH=/);
  }
});
test('Linux absent display variables remain unknown without environment values', { skip: process.platform !== 'linux' }, () => {
  const script = generatePreflight({ platform: 'linux', arch: 'x64' });
  const env = { ...process.env, DISPLAY: '', WAYLAND_DISPLAY: '', PREFLIGHT_SECRET_SENTINEL: 'never-print-me-726' };
  const result = spawnSync('/bin/sh', [], { input: script.content, env, encoding: 'utf8', timeout: 5000 });
  assert.equal(result.status, 0);
  assert.match(result.stdout, /^UNKNOWN DISPLAY_ENV /m);
  assert.match(result.stdout, /^UNKNOWN TRANSITIVE_DEPENDENCIES /m);
  assert.doesNotMatch(result.stdout + result.stderr, /never-print-me-726|\/home\/|\/workspace\//);
  assert.equal(result.stderr, '');
});
test('Linux display candidate does not promise connectivity or turn unknowns into failure', { skip: process.platform !== 'linux' }, () => {
  const script = generatePreflight({ platform: 'linux', arch: 'x64' });
  const result = spawnSync('/bin/sh', [], {
    input: script.content, env: { ...process.env, DISPLAY: 'do-not-print-this-display', WAYLAND_DISPLAY: '' }, encoding: 'utf8', timeout: 5000,
  });
  assert.equal(result.status, 0);
  assert.match(result.stdout, /^PASS DISPLAY_ENV .*not verified/m);
  assert.doesNotMatch(result.stdout, /do-not-print-this-display/);
});
test('Windows diagnostics use read-only official registry discovery, never an execution-policy bypass', () => {
  const script = generatePreflight({ platform: 'windows', arch: 'arm64' });
  assert.equal(script.filename, 'doctor.ps1');
  assert.match(script.content, /F3017226-FE2A-4295-8BDF-00C3A9A7E4C5/);
  assert.match(script.content, /OpenSubKey\([^\n]+, \$false\)/);
  assert.match(script.content, /UNKNOWN WEBVIEW2_REGISTRY_CANDIDATE/);
  assert.doesNotMatch(script.content, /Set-ExecutionPolicy|Bypass|SetValue|Start-Process/);
});
