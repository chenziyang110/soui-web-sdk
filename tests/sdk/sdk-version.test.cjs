'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const versioning = require('../../tools/internal/sdk-version.cjs');
const sdk = require('../../tools/sdk/soui-sdk.cjs');
const builds = require('../../tools/internal/build-evidence.cjs');
const identity = require('../../tools/internal/delivery-identity.cjs');
const output = path.resolve(__dirname, '../../out');
function fixture() {
  fs.mkdirSync(output, {recursive: true});
  const root = fs.mkdtempSync(path.join(output, 'sdk-version-'));
  const source = path.join(root, 'source');
  function write(name, data) {
    const file = path.join(source, name);
    fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, data);
  }
  for (const name of sdk.managedRoots) write(name + '/fixture.txt', 'first content\n');
  for (const name of sdk.managedFiles) write(name, 'first content\n');
  return {root, source, write};
}
test('versions have one canonical preview, RC or stable channel', () => {
  for (const [version, channel] of [['0.5.0-preview.0', 'preview'], ['0.5.0-preview.1', 'preview'],
    ['0.5.0-rc.1', 'rc'], ['0.5.0', 'stable'], ['1.20.300', 'stable']]) {
    assert.equal(versioning.isVersion(version), true, version);
    assert.equal(versioning.channelOf(version), channel);
  }
  for (const version of [null, 5, '', 'v0.5.0', '0.5', '00.5.0', '0.05.0', '0.5.00',
    '0.5.0-preview.01', '0.5.0-RC.1', '0.5.0-beta.1', '0.5.0+build.1', '^0.5.0',
    '0.5.0 ', '0.5.0\n', '0.5.0-rc.-1', '9007199254740992.5.0', '1'.repeat(65)]) {
    assert.equal(versioning.isVersion(version), false, String(version));
    assert.throws(() => versioning.channelOf(version), /Invalid SDK version/);
  }
});
test('channel mismatch and claimed production acceptance cannot enter an SDK lock', () => {
  const f = fixture(), packed = path.join(f.root, 'packed');
  sdk.pack(f.source, packed, '0.5.0');
  const manifest = sdk.readPackage(packed), file = path.join(packed, 'sdk-manifest.json');
  assert.equal(manifest.channel, 'stable'); assert.equal(manifest.releaseReady, false);
  for (const mutation of [{channel: 'preview'}, {channel: 'rc'}, {releaseReady: true}, {version: '0.5.0-beta.1'}]) {
    fs.writeFileSync(file, identity.encoded({...manifest, ...mutation}));
    assert.throws(() => sdk.readPackage(packed), /Invalid/);
  }
  fs.writeFileSync(file, identity.encoded(manifest));
  assert.equal(sdk.readPackage(packed).version, '0.5.0');
});
test('preview to RC to stable installs and rolls back without altering business files', () => {
  const f = fixture(), packages = [], project = path.join(f.root, 'project');
  fs.mkdirSync(path.join(project, 'app'), {recursive: true});
  fs.writeFileSync(path.join(project, 'app/business.txt'), 'business-owned\n');
  for (const version of ['0.5.0-preview.1', '0.5.0-rc.1', '0.5.0']) {
    f.write('sdk/fixture.txt', version + '\n');
    const packed = path.join(f.root, version); packages.push(packed);
    assert.equal(sdk.pack(f.source, packed, version).releaseReady, false);
    assert.equal(sdk.install(packed, project).releaseReady, false);
    const lock = sdk.readLock(project);
    assert.equal(lock.manifest.version, version);
    assert.equal(lock.manifest.channel, versioning.channelOf(version));
    assert.equal(fs.readFileSync(path.join(project, 'app/business.txt'), 'utf8'), 'business-owned\n');
  }
  f.write('sdk/fixture.txt', 'same stable version with different bytes\n');
  const conflicting = path.join(f.root, 'conflicting-stable'); sdk.pack(f.source, conflicting, '0.5.0');
  assert.throws(() => sdk.install(conflicting, project), /different contents/);
  assert.equal(sdk.readLock(project).manifest.version, '0.5.0');
  sdk.install(packages[1], project); assert.equal(sdk.readLock(project).manifest.version, '0.5.0-rc.1');
  sdk.install(packages[0], project); assert.equal(sdk.readLock(project).manifest.version, '0.5.0-preview.1');
});
test('build identity requires matching header and JavaScript metadata for RC and stable', () => {
  const f = fixture();
  for (const version of ['0.5.0-rc.1', '0.5.0']) {
    f.write('VERSION', version + '\n');
    f.write('sdk/include/soui/version.h', '#define SOUI_SDK_VERSION_STRING "' + version + '"\n');
    f.write('sdk/web/package.json', identity.encoded({version}));
    f.write('sdk/web/package-lock.json', identity.encoded({version, packages: {'': {version}}}));
    assert.equal(builds.version(f.source), version);
    f.write('sdk/web/package.json', identity.encoded({version: '0.5.0-preview.1'}));
    assert.throws(() => builds.version(f.source), /differ/);
  }
});
