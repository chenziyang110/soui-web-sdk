'use strict';
// Filesystem evidence fixtures only: synthetic archives/loaders are never run.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const identity = require('../../tools/internal/delivery-identity.cjs');
const evidence = require('../../tools/internal/build-evidence.cjs');

function fixture(t, shared = true) {
  const owner = fs.realpathSync(os.tmpdir());
  const root = fs.mkdtempSync(path.join(owner, 'soui-loader-evidence-'));
  t.after(() => {
    assert.equal(path.dirname(root), owner);
    assert.ok(path.basename(root).startsWith('soui-loader-evidence-'));
    fs.rmSync(identity.safePath(root), {recursive: true, force: true});
  });
  const put = (name, value) => {
    const file = identity.safePath(root, name);
    fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, value); return file;
  };
  const version = '0.6.1-preview.1';
  put('VERSION', version);
  put('sdk/include/soui/version.h', '#define SOUI_SDK_VERSION_STRING "' + version + '"');
  put('sdk/web/package.json', JSON.stringify({version}));
  put('sdk/web/package-lock.json', JSON.stringify({version, packages: {'': {version}}}));
  put('out/sdk-x64/Release/soui_sdk.lib', 'synthetic SDK archive');
  put('out/sdk-x64/renderers/webview2/Release/soui_webview2.lib', 'synthetic WebView2 archive');
  const loader = put('out/dependency/build/native/x64/' + (shared ? 'WebView2Loader.dll' : 'WebView2LoaderStatic.lib'), 'synthetic loader');
  const cache = (enabled, selected = loader) => put('out/sdk-x64/CMakeCache.txt',
    'SOUI_BUILD_WEBVIEW2:BOOL=ON\nSOUI_WEBVIEW2_LOADER_SHARED:BOOL=' + (enabled ? 'ON' : 'OFF') + '\n' +
    (enabled ? 'SOUI_WEBVIEW2_SHARED_LOADER' : 'SOUI_WEBVIEW2_STATIC_LOADER') + ':FILEPATH=' + selected.replaceAll('\\', '/') + '\n');
  cache(shared);
  const reportFile = path.join(root, 'out/sdk-x64/build-Release.json');
  const report = () => identity.readJson(reportFile);
  const rewrite = mutate => {const value = report(); mutate(value); fs.writeFileSync(reportFile, identity.encoded(value));};
  const build = () => {evidence.start(root, 'x64', 'Release', true); evidence.finish(root, 'x64', 'Release', true); return report();};
  const verify = () => evidence.verifyBuild(root, 'x64', 'Release', true);
  return {root, put, loader, cache, report, rewrite, build, verify};
}

test('SDK-only evidence pins the actual shared Loader input and mode', t => {
  const f = fixture(t), built = f.build();
  assert.deepEqual(built.webview2Loader, {mode: 'shared', input: {
    path: f.loader, bytes: fs.statSync(f.loader).size, sha256: identity.hash(fs.readFileSync(f.loader))}});
  assert.equal(f.verify().nativeBuild, 'passed');
});

for (const mutation of ['bytes', 'missing', 'mode', 'selected-path']) {
  test('SDK-only verification rejects shared Loader ' + mutation + ' drift after recording', t => {
    const f = fixture(t); f.build();
    if (mutation === 'bytes') fs.writeFileSync(f.loader, 'synthetic loadeR');
    if (mutation === 'missing') fs.unlinkSync(f.loader);
    if (mutation === 'mode') f.cache(false);
    if (mutation === 'selected-path') {
      const other = f.put('out/dependency-copy/WebView2Loader.dll', fs.readFileSync(f.loader)); f.cache(true, other);
    }
    assert.throws(f.verify, /Loader|loader|ENOENT/);
  });
}

test('a shared build cannot omit the new input pin and reuse legacy static evidence', t => {
  const f = fixture(t); f.build(); f.rewrite(value => {delete value.webview2Loader;});
  assert.throws(f.verify, /Loader|loader/);
});

test('new static evidence pins its selected archive, while unpinned historical static reports retain their old scope', t => {
  const f = fixture(t, false), built = f.build();
  assert.equal(built.webview2Loader?.mode, 'static');
  assert.equal(built.webview2Loader.input.path, f.loader);
  fs.writeFileSync(f.loader, 'changed static loader');
  assert.throws(f.verify, /Loader|loader/);
  f.rewrite(value => {delete value.webview2Loader;});
  assert.equal(f.verify().nativeBuild, 'passed');
  assert.equal(f.report().webview2Loader, undefined, 'readers do not backfill historical records');
});

test('a shared build refuses to record an absent selected Loader cache input', t => {
  const f = fixture(t);
  f.put('out/sdk-x64/CMakeCache.txt', 'SOUI_BUILD_WEBVIEW2:BOOL=ON\nSOUI_WEBVIEW2_LOADER_SHARED:BOOL=ON\n');
  evidence.start(f.root, 'x64', 'Release', true);
  assert.throws(() => evidence.finish(f.root, 'x64', 'Release', true), /Loader|loader/);
  assert.equal(f.report().nativeBuild, 'pending');
});
