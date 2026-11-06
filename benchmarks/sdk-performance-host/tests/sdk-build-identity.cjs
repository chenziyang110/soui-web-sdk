'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const { inspect, freeze, verify } = require('../freeze-sdk-build.cjs');
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const encoded = value => JSON.stringify(value, null, 2) + '\n';

function crc32(bytes) {
  let value = 0xffffffff;
  for (const byte of bytes) {
    value ^= byte;
    for (let bit = 0; bit < 8; ++bit) value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
  }
  return (value ^ 0xffffffff) >>> 0;
}

function storedZip(entryName, payload) {
  const name = Buffer.from(entryName, 'utf8'), checksum = crc32(payload);
  const local = Buffer.alloc(30 + name.length + payload.length);
  local.writeUInt32LE(0x04034b50, 0); local.writeUInt16LE(20, 4); local.writeUInt16LE(0, 6);
  local.writeUInt16LE(0, 8); local.writeUInt32LE(checksum, 14); local.writeUInt32LE(payload.length, 18);
  local.writeUInt32LE(payload.length, 22); local.writeUInt16LE(name.length, 26); name.copy(local, 30); payload.copy(local, 30 + name.length);
  const central = Buffer.alloc(46 + name.length);
  central.writeUInt32LE(0x02014b50, 0); central.writeUInt16LE(20, 4); central.writeUInt16LE(20, 6);
  central.writeUInt16LE(0, 8); central.writeUInt16LE(0, 10); central.writeUInt32LE(checksum, 16);
  central.writeUInt32LE(payload.length, 20); central.writeUInt32LE(payload.length, 24);
  central.writeUInt16LE(name.length, 28); central.writeUInt32LE(0, 42); name.copy(central, 46);
  const end = Buffer.alloc(22);
  end.writeUInt32LE(0x06054b50, 0); end.writeUInt16LE(1, 8); end.writeUInt16LE(1, 10);
  end.writeUInt32LE(central.length, 12); end.writeUInt32LE(local.length, 16);
  return Buffer.concat([local, central, end]);
}

function fixture(t) {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-sdk-build-identity-'));
  t.after(() => {
    assert.equal(path.dirname(root), path.resolve(os.tmpdir()));
    assert.ok(path.basename(root).startsWith('soui-sdk-build-identity-'));
    fs.rmSync(root, { recursive: true, force: true });
  });
  const prefix = path.join(root, 'prefix');
  function write(name, value) {
    const file = path.join(prefix, name); fs.mkdirSync(path.dirname(file), { recursive: true }); fs.writeFileSync(file, value); return file;
  }
  function writeExternal(name, value) {
    const file = path.join(root, name); fs.writeFileSync(file, value); return file;
  }
  const versionHeader = '#pragma once\r\n#define SOUI_SDK_VERSION_STRING "0.7.0-preview.2"\r\n';
  write('include/soui/version.h', versionHeader);
  write('include/soui/action-json/json.hpp', '// json\n');
  write('lib/Release/soui_sdk.lib', 'sdk-lib');
  write('lib/Release/soui_webview2.lib', 'webview2-lib');
  const loaderBytes = Buffer.from('loader');
  const archiveBytes = storedZip('build/native/x64/WebView2LoaderStatic.lib', loaderBytes);
  write('lib/webview2/WebView2LoaderStatic.lib', loaderBytes);
  write('share/SouiSdk/LICENSE.md', 'license');
  const archive = writeExternal('microsoft.web.webview2.1.0.0.0.nupkg', archiveBytes);
  const dependencyDescriptor = writeExternal('dependency.json', encoded({ schema: 'soui.optional-dependency/1',
    id: 'Microsoft.Web.WebView2', version: '1.0.0.0', url: 'https://example.invalid/microsoft.web.webview2.1.0.0.0.nupkg',
    sha256: hash(archiveBytes) }));
  const sources = [
    { path: 'sdk/include/soui/version.h', bytes: Buffer.byteLength(versionHeader.replace(/\r\n/g, '\n')),
      sha256: hash(Buffer.from(versionHeader.replace(/\r\n/g, '\n'))), normalization: 'lf' },
    { path: 'third_party/include/json/json.hpp', bytes: 8, sha256: hash('// json\n'), normalization: 'lf' },
    { path: 'sdk/renderers/webview2/dependency.json', bytes: fs.statSync(dependencyDescriptor).size,
      sha256: hash(fs.readFileSync(dependencyDescriptor)), normalization: 'lf' }
  ];
  const build = { schema: 'soui.source-build/v2', buildId: 'build-1', sdkOnly: true, nativeBuild: 'passed',
    nativeToolchain: { profile: 'MSVC143' }, version: '0.7.0-preview.2', architecture: 'x64', configuration: 'Release',
    source: { files: sources, sha256: hash(encoded(sources)) },
    library: { files: [{ path: 'Release/soui_sdk.lib', bytes: 7, sha256: hash('sdk-lib') }] },
    optionalLibraries: { webview2: { files: [{ path: 'renderers/webview2/Release/soui_webview2.lib', bytes: 12, sha256: hash('webview2-lib') }] } } };
  const receipt = writeExternal('build.json', encoded(build));
  return { root, prefix, receipt, archive, dependencyDescriptor, build, write, writeExternal,
    options: { prefix, receipt, archive, dependencyDescriptor, version: '0.7.0', architecture: 'x64', configuration: 'Release' },
    manifest: path.join(root, 'inputs.json'), header: path.join(root, 'frozen.h'),
    update() { fs.writeFileSync(receipt, encoded(build)); } };
}

test('binds installed headers, SDK libraries and the pinned WebView2 archive into a safe C++ literal', async t => {
  const f = fixture(t), summary = await freeze(f.options, f.manifest, f.header);
  assert.equal(summary.sourceSha256, f.build.source.sha256);
  assert.equal(summary.sdkBuildId, 'build-1'); assert.equal(summary.headerCount, 2);
  assert.equal(summary.sdkNativeToolchain, 'MSVC143');
  assert.equal(summary.installedFileCount, 6); assert.equal(summary.libraries.sdk.sha256, hash('sdk-lib'));
  assert.equal(summary.sdkVersion, '0.7.0-preview.2'); assert.equal(JSON.stringify(summary).includes(f.root), false);
  assert.equal(summary.libraries.loader.packageSha256, hash(fs.readFileSync(f.archive)));
  assert.equal(summary.libraries.loader.sha256, hash('loader'));
  assert.deepEqual(await verify(f.options, f.manifest), summary);
  const header = fs.readFileSync(f.header, 'utf8');
  assert.ok(header.includes('frozen_sdk_build_json[] = "{\\"schema\\"'));
  assert.ok(!header.includes('R"SOUI_PERF('));
});

test('rejects mixed versions, architectures, configurations and non-SDK or failed receipts', async t => {
  for (const [field, value] of [['version', '0.6.0'], ['architecture', 'x86'], ['configuration', 'Debug'],
    ['nativeBuild', 'failed'], ['sdkOnly', false]]) {
    const f = fixture(t); f.build[field] = value; f.update(); await assert.rejects(inspect(f.options));
  }
});

test('rejects invalid, overlong or installed-header-mismatched release identities before code generation', async t => {
  for (const version of ['0.7.0-untrusted', '0.7.0-x)SOUI_PERF"; int injected=1; //',
    '0.7.0-preview.' + '1'.repeat(60)]) {
    const f = fixture(t); f.build.version = version; f.update();
    await assert.rejects(inspect(f.options), /version/i);
  }
  const mismatched = fixture(t); mismatched.build.version = '0.7.0-rc.3'; mismatched.update();
  await assert.rejects(inspect(mismatched.options), /version header differs/);
});

test('rejects altered source manifests and modified or unlisted installed headers', async t => {
  const broken = fixture(t); broken.build.source.sha256 = '0'.repeat(64); broken.update();
  await assert.rejects(inspect(broken.options), /source manifest digest/);
  const changed = fixture(t); changed.write('include/soui/version.h', '#define SOUI_SDK_VERSION_STRING "old"\n');
  await assert.rejects(inspect(changed.options), /header differs/);
  const extra = fixture(t); extra.write('include/soui/unrecorded.h', '// unrecorded');
  await assert.rejects(inspect(extra.options), /header is absent/);
});

test('rejects changed SDK libraries, missing or altered loader and a package archive that is not pinned', async t => {
  for (const name of ['soui_sdk.lib', 'soui_webview2.lib']) {
    const f = fixture(t); f.write('lib/Release/' + name, 'changed');
    await assert.rejects(inspect(f.options), /library differs/);
  }
  const changedLoader = fixture(t); changedLoader.write('lib/webview2/WebView2LoaderStatic.lib', 'different');
  await assert.rejects(inspect(changedLoader.options), /loader differs from the pinned dependency archive/);
  const missingLoader = fixture(t); fs.unlinkSync(path.join(missingLoader.prefix, 'lib/webview2/WebView2LoaderStatic.lib'));
  await assert.rejects(inspect(missingLoader.options), /loader differs from the pinned dependency archive/);
  const changedArchive = fixture(t); fs.writeFileSync(changedArchive.archive, 'not the pinned archive');
  await assert.rejects(inspect(changedArchive.options), /archive differs from pinned dependency/);
  const changedDescriptor = fixture(t); fs.writeFileSync(changedDescriptor.dependencyDescriptor, '{}\n');
  await assert.rejects(inspect(changedDescriptor.options), /dependency descriptor differs from SDK build sources/);
});

test('refuses identity outputs that could overwrite receipt, prefix, archive, descriptor or each other', async t => {
  const f = fixture(t), originalReceipt = fs.readFileSync(f.receipt);
  await assert.rejects(freeze(f.options, f.receipt, f.header), /overlaps receipt/);
  assert.deepEqual(fs.readFileSync(f.receipt), originalReceipt);
  const inPrefix = path.join(f.prefix, 'include/soui/new_identity.h');
  await assert.rejects(freeze(f.options, inPrefix, f.header), /overlaps prefix/);
  assert.equal(fs.existsSync(inPrefix), false);
  await assert.rejects(freeze(f.options, f.manifest, f.manifest), /outputs overlap/);
  const archiveBefore = fs.readFileSync(f.archive);
  await assert.rejects(freeze(f.options, f.archive, f.header), /overlaps archive/);
  assert.deepEqual(fs.readFileSync(f.archive), archiveBefore);
});

test('each build revalidates the complete installed prefix and immutable receipt against configure-time identity', async t => {
  const f = fixture(t); await freeze(f.options, f.manifest, f.header); f.write('share/SouiSdk/LICENSE.md', 'changed');
  await assert.rejects(verify(f.options, f.manifest), /Frozen installed SDK inputs/);
  const other = fixture(t); await freeze(other.options, other.manifest, other.header);
  other.build.buildId = 'build-2'; other.update();
  await assert.rejects(verify(other.options, other.manifest), /Frozen installed SDK inputs/);
});
