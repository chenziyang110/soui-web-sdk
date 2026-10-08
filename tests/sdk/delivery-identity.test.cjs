'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const identity = require('../../tools/internal/delivery-identity.cjs');
const archive = require('../../tools/internal/source-archive.cjs');
const deploy = require('../../tools/internal/deploy-resources.cjs');
const evidence = require('../../tools/internal/build-evidence.cjs');
const sdk = require('../../tools/sdk/soui-sdk.cjs');

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-delivery-'));
  t.after(() => fs.rmSync(root, {recursive: true, force: true}));
  return root;
}
function write(root, name, data) {
  const file = path.join(root, name);
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, data);
}
function git(root, ...args) {
  return cp.execFileSync('git', ['-C', root, ...args], {encoding: 'utf8', windowsHide: true});
}
function unpackedSource(t) {
  const root = fixture(t);
  const files = new Map([
    ['VERSION', Buffer.from('0.5.0-preview.1\n')],
    ['src/main.cpp', Buffer.from('int main() {}\n')],
    ['verification/raw/failed/out/probe.exe', Buffer.from([0x4d, 0x5a, 0, 13, 10, 255])],
  ]);
  for (const [name, data] of files) write(root, name, data);
  const manifest = {schema: 'soui.full-source/v2', version: '0.5.0-preview.1', releaseReady: false,
    files: [...files].map(([name, data]) => ({path: name, bytes: data.length, sha256: identity.hash(data), normalization: 'none'}))};
  write(root, 'source-manifest.json', identity.encoded(manifest));
  return {root, files, manifest};
}

test('source snapshot excludes ignored cache, works with CRLF checkout and without Git', t => {
  const root = fixture(t);
  git(root, 'init', '-q');
  write(root, '.gitattributes', '* text=auto\n*.ps1 text eol=crlf\n*.dll -text\n');
  write(root, '.gitignore', '*.cache\n');
  write(root, 'VERSION', '0.3.0-preview.1\n');
  write(root, 'tools/test.ps1', 'Write-Output hello\r\n');
  write(root, 'binary.dll', Buffer.from([0, 13, 10, 255]));
  write(root, 'textual.dll', 'MZ\r\ntext-like binary\r\n');
  write(root, 'mixed.txt', 'first\r\r\nsecond\r\n');
  write(root, 'ignored.cache', 'not source');
  git(root, 'add', '.');
  const manifest = archive.create(root);
  assert.equal(manifest.files.some(f => f.path.endsWith('.cache')), false);
  archive.verify(root);
  const stage = path.join(fixture(t), 'export');
  archive.stage(root, stage);
  assert.equal(fs.existsSync(path.join(stage, '.git')), false);
  archive.verify(stage);
  git(stage, 'init', '-q');
  git(stage, 'add', '.');
  const regenerated = archive.create(stage);
  assert.deepEqual(regenerated.files, manifest.files);
  assert.deepEqual(fs.readFileSync(path.join(stage, 'binary.dll')), Buffer.from([0, 13, 10, 255]));
  assert.equal(fs.readFileSync(path.join(stage, 'textual.dll'), 'utf8'), 'MZ\r\ntext-like binary\r\n');
  write(stage, 'tools/test.ps1', 'Write-Output hello\r\n');
  archive.verify(stage);
  write(stage, 'tools/test.ps1', 'Write-Output jello\r\n');
  assert.throws(() => archive.verify(stage), /modified/);
});

test('source export preserves tracked historical output paths and excludes unrelated build caches', t => {
  const root = fixture(t);
  git(root, 'init', '-q');
  write(root, '.gitattributes', '* -text\n');
  write(root, '.gitignore', 'out/\ndist/\ndist-legacy/\nnode_modules/\n.vs/\n');
  write(root, 'VERSION', '0.5.0-preview.1\n');
  const bytes = Buffer.from([0x4d, 0x5a, 0, 13, 10, 255]);
  const names = ['out/probe.exe', 'dist/assets/probe.bin', 'dist-legacy/probe.bin',
    'node_modules/frozen/probe.bin', '.vs/probe.bin'].map(name => `verification/raw/failed/${name}`);
  for (const name of names) write(root, name, bytes);
  write(root, 'out/current-build/ignored.exe', 'build cache');
  git(root, 'add', '.');
  git(root, 'add', '-f', '--', ...names);
  const manifest = archive.create(root);
  assert.equal(manifest.schema, 'soui.full-source/v2');
  for (const name of names) assert.equal(manifest.files.some(file => file.path === name), true, name);
  const stage = path.join(fixture(t), 'export');
  const result = archive.stage(root, stage);
  assert.equal(result.files, manifest.files.length);
  assert.equal(fs.existsSync(path.join(stage, '.git')), false);
  assert.equal(fs.existsSync(path.join(stage, 'out')), false);
  for (const name of names) assert.deepEqual(fs.readFileSync(path.join(stage, name)), bytes, name);
  for (const cache of ['out', 'dist', 'dist-legacy', 'node_modules', '.vs']) {
    write(stage, `${cache}/current-build/ignored.bin`, 'new build cache');
  }
  assert.deepEqual(archive.verify(stage).files, manifest.files);
});

test('unpacked source rejects missing or equal-length modified historical binaries', t => {
  const {root, files} = unpackedSource(t);
  const name = 'verification/raw/failed/out/probe.exe';
  archive.verify(root);
  fs.unlinkSync(path.join(root, name));
  assert.throws(() => archive.verify(root), /Missing|missing|file set/);
  const modified = Buffer.from(files.get(name));
  modified[modified.length - 1] = 254;
  write(root, name, modified);
  assert.throws(() => archive.verify(root), /modified/);
  write(root, name, files.get(name));
  archive.verify(root);
});

test('unpacked source rejects undeclared files including nested caches inside frozen output trees', t => {
  const {root} = unpackedSource(t);
  archive.verify(root);
  for (const name of ['extra.cpp', 'verification/raw/failed/out/extra.exe',
    'verification/raw/failed/out/out/extra.exe', 'verification/raw/failed/out/node_modules/extra.js']) {
    write(root, name, 'not declared');
    assert.throws(() => archive.verify(root), /file set/, name);
    fs.unlinkSync(path.join(root, name));
    archive.verify(root);
  }
});

test('unpacked source rejects duplicate, escaping and malformed manifest paths', t => {
  const {root, manifest} = unpackedSource(t);
  archive.verify(root);
  for (const name of ['version', '../outside.exe', 'C:/outside.exe', 'raw/./probe.exe',
    'raw//probe.exe', 'raw/NUL', 'raw/probe.exe:stream', 'raw\\probe.exe']) {
    const changed = {...manifest, files: [...manifest.files, {...manifest.files[0], path: name}]};
    write(root, 'source-manifest.json', identity.encoded(changed));
    assert.throws(() => archive.verify(root), /Invalid|duplicate/, name);
  }
  write(root, 'source-manifest.json', identity.encoded(manifest));
  archive.verify(root);
});

test('unpacked source rejects linked historical output directories without changing the target', t => {
  const {root, files} = unpackedSource(t);
  archive.verify(root);
  const name = 'verification/raw/failed/out/probe.exe';
  const outside = fixture(t);
  write(outside, 'probe.exe', files.get(name));
  fs.unlinkSync(path.join(root, name));
  fs.rmdirSync(path.join(root, 'verification/raw/failed/out'));
  fs.symlinkSync(outside, path.join(root, 'verification/raw/failed/out'), 'junction');
  assert.throws(() => archive.verify(root), /Linked/);
  assert.deepEqual(fs.readFileSync(path.join(outside, 'probe.exe')), files.get(name));
});

test('unpacked source cannot hide a linked directory under an excluded cache name', t => {
  const {root} = unpackedSource(t);
  archive.verify(root);
  const outside = fixture(t);
  write(outside, 'protected.txt', 'keep');
  fs.symlinkSync(outside, path.join(root, 'out'), 'junction');
  assert.throws(() => archive.verify(root), /Linked/);
  assert.equal(fs.readFileSync(path.join(outside, 'protected.txt'), 'utf8'), 'keep');
});

test('source validation rejects missing, extra, ambiguous and escaping files', t => {
  const root = fixture(t);
  git(root, 'init', '-q');
  write(root, 'VERSION', '0.3.0-preview.1\n');
  write(root, 'a.txt', 'A\n');
  git(root, 'add', '.');
  archive.create(root);
  write(root, 'extra.cpp', 'unreviewed');
  assert.throws(() => archive.verify(root), /file set|Untracked/);
  fs.unlinkSync(path.join(root, 'extra.cpp'));
  fs.unlinkSync(path.join(root, 'a.txt'));
  assert.throws(() => archive.verify(root), /missing|Missing/);
  for (const name of ['../bad', 'C:/bad', 'a/./b', 'x/NUL', 'a:stream', 'a\\b']) {
    assert.throws(() => identity.safePath(root, name), /Invalid/);
  }
  write(root, 'a.txt', 'A\n');
  const manifest = JSON.parse(fs.readFileSync(path.join(root, 'source-manifest.json')));
  manifest.files.push({...manifest.files[0]});
  write(root, 'source-manifest.json', JSON.stringify(manifest));
  assert.throws(() => archive.verify(root), /duplicate|Duplicate/);
});

test('license bytes survive Git checkout and full-source export under repository attributes', t => {
  const root = fixture(t);
  git(root, 'init', '-q');
  git(root, 'config', 'core.autocrlf', 'true');
  write(root, '.gitattributes', fs.readFileSync(path.join(__dirname, '../../.gitattributes')));
  write(root, 'VERSION', '0.3.0-preview.1\n');
  const licenses = new Map([
    ['third_party/licenses/element-plus-2.11.1-LICENSE.txt', Buffer.from('License\r\nCopyright fixture\r\n')],
    ['third_party/licenses/future-component-LICENSE.md', Buffer.from('License\r\nKeep original bytes\r\n')],
  ]);
  for (const [name, bytes] of licenses) write(root, name, bytes);
  const before = identity.source(root);
  git(root, 'add', '.');
  archive.create(root);
  const stage = path.join(fixture(t), 'export');
  archive.stage(root, stage);
  for (const [name, bytes] of licenses) {
    assert.deepEqual(fs.readFileSync(path.join(stage, name)), bytes, name);
    fs.unlinkSync(path.join(root, name));
    git(root, 'checkout-index', '--', name);
    assert.deepEqual(fs.readFileSync(path.join(root, name)), bytes, name);
  }
  assert.equal(identity.source(stage).sha256, before.sha256);
  assert.equal(identity.source(root).sha256, before.sha256);
});

test('payload evidence detects equal-length page edits, new files and missing DLLs', t => {
  const root = fixture(t);
  write(root, 'App.exe', 'exe');
  write(root, 'node.dll', 'dll');
  write(root, 'resources/index.html', 'first');
  const expected = identity.payload(root, 'App');
  write(root, 'smoke.log', 'diagnostic');
  identity.verifyPayload(root, 'App', expected);
  write(root, 'resources/index.html', 'other');
  assert.throws(() => identity.verifyPayload(root, 'App', expected), /changed|drift/);
  write(root, 'resources/index.html', 'first');
  write(root, 'resources/old.js', 'stale');
  assert.throws(() => identity.verifyPayload(root, 'App', expected), /changed|drift/);
  fs.unlinkSync(path.join(root, 'resources/old.js'));
  fs.unlinkSync(path.join(root, 'node.dll'));
  assert.throws(() => identity.verifyPayload(root, 'App', expected), /missing|Missing/);
});

test('modern payload does not require Miniblink but preserves full asset identity checks', t => {
  const root = fixture(t);
  write(root, 'App.exe', 'exe');
  write(root, 'resources/index.html', 'first');
  assert.throws(() => identity.payload(root, 'App'), /node.dll/);
  const report = identity.payload(root, 'App', 'webview2');
  identity.verifyPayload(root, 'App', report, 'webview2');
  write(root, 'resources/index.html', 'other');
  assert.throws(() => identity.verifyPayload(root, 'App', report, 'webview2'), /changed/);
  assert.throws(() => identity.payload(root, 'App', 'unknown'), /Unknown renderer/);
});

test('deployment replaces stale Web assets and preserves app data outside resources', t => {
  const root = fixture(t);
  const source = path.join(root, 'web');
  const app = path.join(root, 'app');
  write(source, 'index.html', 'new');
  write(source, 'assets/current.js', 'current');
  write(root, 'THIRD_PARTY_NOTICES.md', 'SDK notices');
  write(root, 'third_party/licenses/example-LICENSE.txt', 'Dependency license');
  write(app, 'resources/index.html', 'old');
  write(app, 'resources/assets/obsolete.js', 'obsolete');
  write(app, 'resources/legal/stale.txt', 'stale notice');
  write(app, 'user-data/preferences.json', '{}');
  deploy.replace(source, app, root);
  assert.equal(fs.existsSync(path.join(app, 'resources/assets/obsolete.js')), false);
  assert.equal(fs.existsSync(path.join(app, 'resources/legal/stale.txt')), false);
  assert.equal(fs.readFileSync(path.join(app, 'resources/index.html'), 'utf8'), 'new');
  assert.equal(fs.readFileSync(path.join(app, 'resources/legal/THIRD_PARTY_NOTICES.md'), 'utf8'), 'SDK notices');
  assert.equal(fs.readFileSync(path.join(app, 'resources/legal/third_party/licenses/example-LICENSE.txt'), 'utf8'), 'Dependency license');
  assert.equal(fs.readFileSync(path.join(app, 'user-data/preferences.json'), 'utf8'), '{}');
  assert.throws(() => deploy.replace(path.join(root, 'missing'), app, root), /ENOENT|missing/);
  assert.equal(fs.readFileSync(path.join(app, 'resources/index.html'), 'utf8'), 'new');
  write(source, 'legal/unreviewed.txt', 'collision');
  assert.throws(() => deploy.replace(source, app, root), /reserved legal path/);
  fs.unlinkSync(path.join(source, 'legal/unreviewed.txt'));
  fs.unlinkSync(path.join(root, 'THIRD_PARTY_NOTICES.md'));
  assert.throws(() => deploy.replace(source, app, root), /ENOENT|missing/);
  assert.equal(fs.readFileSync(path.join(app, 'resources/legal/THIRD_PARTY_NOTICES.md'), 'utf8'), 'SDK notices');
  write(root, 'THIRD_PARTY_NOTICES.md', 'SDK notices');
  const before = identity.source(root).sha256;
  fs.writeFileSync(path.join(root, 'third_party/licenses/example-LICENSE.txt'), 'Altered license');
  assert.notEqual(identity.source(root).sha256, before);
});

test('linked resource directories are rejected before deployment changes', t => {
  const root = fixture(t);
  write(root, 'source/index.html', 'new');
  write(root, 'THIRD_PARTY_NOTICES.md', 'SDK notices');
  write(root, 'third_party/licenses/example-LICENSE.txt', 'Dependency license');
  write(root, 'outside/protected.txt', 'keep');
  fs.mkdirSync(path.join(root, 'app'));
  fs.symlinkSync(path.join(root, 'outside'), path.join(root, 'app/resources'), 'junction');
  assert.throws(() => deploy.replace(path.join(root, 'source'), path.join(root, 'app'), root), /Linked/);
  assert.equal(fs.readFileSync(path.join(root, 'outside/protected.txt'), 'utf8'), 'keep');
});

test('SDK packages are identical across source EOLs but installed bytes remain pinned', t => {
  const root = fixture(t);
  const source = path.join(root, 'source');
  const licenseBytes = Buffer.from([0x4c, 0x69, 0x63, 0x65, 0x6e, 0x73, 0x65, 13, 10, 0xa9, 13, 10]);
  for (const name of sdk.managedRoots) write(source, `${name}/fixture.txt`,
    name === 'third_party/licenses' ? licenseBytes : 'one\r\ntwo\r\n');
  for (const name of sdk.managedFiles) write(source, name, 'tool\r\n');
  write(source, 'sdk/legacy.h', Buffer.from([0xd6, 0xd0, 0xce, 0xc4, 13, 10]));
  sdk.pack(source, path.join(root, 'crlf'), '0.3.0-preview.1');
  for (const name of sdk.managedRoots) write(source, `${name}/fixture.txt`,
    name === 'third_party/licenses' ? licenseBytes : 'one\ntwo\n');
  for (const name of sdk.managedFiles) write(source, name, 'tool\n');
  write(source, 'sdk/legacy.h', Buffer.from([0xd6, 0xd0, 0xce, 0xc4, 10]));
  sdk.pack(source, path.join(root, 'lf'), '0.3.0-preview.1');
  assert.deepEqual(sdk.readPackage(path.join(root, 'crlf')), sdk.readPackage(path.join(root, 'lf')));
  const project = path.join(root, 'app');
  sdk.install(path.join(root, 'lf'), project);
  assert.deepEqual(fs.readFileSync(path.join(root, 'lf/payload/third_party/licenses/fixture.txt')), licenseBytes);
  assert.deepEqual(fs.readFileSync(path.join(project, 'third_party/licenses/fixture.txt')), licenseBytes);
  write(project, 'sdk/fixture.txt', 'one\r\ntwo\r\n');
  assert.throws(() => sdk.readLock(project), /modified/);
});

test('build and smoke receipts fail closed after changes, empty runs or legacy successes', t => {
  const root = fixture(t);
  const version = '0.3.0-preview.1';
  write(root, 'VERSION', version);
  write(root, 'sdk/include/soui/version.h', `#define SOUI_SDK_VERSION_STRING "${version}"`);
  write(root, 'sdk/web/package.json', JSON.stringify({version}));
  write(root, 'sdk/web/package-lock.json', JSON.stringify({version, packages: {'': {version}}}));
  for (const target of ['SouiStarter', 'SouiWorkbench']) {
    write(root, `out/x64/bin/Release/${target}/${target}.exe`, 'exe');
    write(root, `out/x64/bin/Release/${target}/node.dll`, 'dll');
    write(root, `out/x64/bin/Release/${target}/resources/index.html`, 'page');
  }
  const file = 'out/x64/build-Release.json';
  write(root, file, JSON.stringify({nativeBuild: 'passed'}));
  assert.throws(() => evidence.verifyBuild(root, 'x64', 'Release'), /stale/);
  evidence.start(root, 'x64', 'Release');
  evidence.finish(root, 'x64', 'Release');
  const metadata = evidence.beginSmoke(root, 'x64', 'Release');
  const build = evidence.verifyBuild(root, 'x64', 'Release');
  assert.throws(() => evidence.validateRuns(root, {...metadata, runs: []}, build), /incomplete/);
  const runs = metadata.scenarios.map(s => ({...s, passed: true, exitCode: 0, timedOut: false, log: 'application-ready host-close-complete'}));
  write(root, 'out/x64/smoke-Release.json', JSON.stringify({...metadata, runs}));
  evidence.finishSmoke(root, 'x64', 'Release');
  evidence.verifySmoke(root, 'x64', 'Release');
  write(root, 'out/x64/bin/Release/SouiStarter/resources/index.html', 'edit');
  assert.throws(() => evidence.verifySmoke(root, 'x64', 'Release'), /changed/);
  write(root, 'out/x64/bin/Release/SouiStarter/resources/index.html', 'page');
  write(root, 'sdk/new.cpp', 'new source');
  assert.throws(() => evidence.verifySmoke(root, 'x64', 'Release'), /stale/);
  evidence.start(root, 'x64', 'Release');
  assert.throws(() => evidence.verifyBuild(root, 'x64', 'Release'), /incomplete/);
  write(root, 'sdk/new.cpp', 'changed during build');
  assert.throws(() => evidence.finish(root, 'x64', 'Release'), /changed during build/);
});
