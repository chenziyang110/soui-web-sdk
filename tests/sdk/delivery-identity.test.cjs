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
