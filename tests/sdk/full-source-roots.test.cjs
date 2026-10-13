'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const id = require('../../tools/internal/delivery-identity.cjs');
const archive = require('../../tools/internal/source-archive.cjs');
const roots = require('../../tools/internal/full-source-roots.cjs');
function write(root, name, data) {
  const file = path.join(root, name);
  fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, data);
  return file;
}
function pin(file) { const data = fs.readFileSync(file); return {path: file, bytes: data.length, sha256: id.hash(data)}; }
function rows(root, names, canonical = false) {
  return names.map(name => {
    const raw = fs.readFileSync(path.join(root, name)), mode = canonical ? 'lf' : 'none';
    const data = id.normalize(raw, mode);
    return {path: name, bytes: data.length, sha256: id.hash(data), normalization: mode};
  });
}
function fixture(t) {
  const base = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-full-source-'));
  t.after(() => fs.rmSync(base, {recursive: true, force: true}));
  // Real deployment shape: publicSource locates a repository which also holds
  // immutable SDK/kernel inputs under out. Its selected subtrees are disjoint.
  const sdk = path.join(base, 'out/sdk/source'), kernel = path.join(base, 'out/kernel/source');
  const sdkNames = ['VERSION', 'source-manifest.json', 'tools/marker.cjs', 'third_party/prebuilt/x64/node.dll'];
  for (const name of sdkNames) write(sdk, name, name === 'VERSION' ? '0.5.0-preview.1\n' : `historical ${name}\n`);
  const kernelNames = ['SOURCE-MANIFEST.json', 'LOCAL-CHANGES.patch', 'wke/current-model.cpp', 'build/miniblink/miniblink.vcxproj'];
  for (const name of kernelNames) write(kernel, name, `current raw ${name}\r\n`);
  const publicNames = ['third_party/sources/soui/SOURCE-MANIFEST.json', 'third_party/sources/soui/SOUI/source.cpp',
    '.github/workflows/build.yml', 'experiments/webview2-consumer/main.cpp', 'third_party/patches/miniblink/current.patch'];
  for (const name of publicNames) write(base, name, `public ${name}\r\n`);
  write(base, 'verification/production-validation/old.json', 'historical receipt\n');
  const sdkManifest = {schema: 'soui.sdk-renderer-v2-abi-raw-source-manifest/v1', root: sdk, sourceOnly: true,
    files: rows(sdk, sdkNames).map(row => ({relative: row.path, pin: {path: path.join(sdk, row.path), bytes: row.bytes, sha256: row.sha256}}))};
  const kernelManifest = {schema: 'soui.full-kernel-composed-source/v1', copySourceRoot: kernel, sourceOnly: true, executionEnabled: false,
    files: rows(kernel, kernelNames).map(row => ({relative: row.path, bytes: row.bytes, sha256: row.sha256}))};
  const publicManifest = {schema: 'soui.full-source/v2', version: '0.5.0-preview.1', releaseReady: false,
    files: [...rows(base, publicNames, true), ...rows(base, ['verification/production-validation/old.json'])]};
  const binding = {schema: 'soui.full-source-roots/v1',
    sdk: {root: sdk, manifest: pin(write(base, 'pins/sdk.json', id.encoded(sdkManifest)))},
    miniblink: {root: kernel, manifest: pin(write(base, 'pins/kernel.json', id.encoded(kernelManifest)))},
    publicSource: {root: base, manifest: pin(write(base, 'pins/public.json', id.encoded(publicManifest)))}};
  const bindingFile = write(base, 'binding.json', id.encoded(binding));
  const output = path.join(base, 'out/export');
  return {base, sdk, kernel, binding, bindingFile, output, publicNames, kernelNames,
    save: () => write(base, 'binding.json', id.encoded(binding))};
}
test('explicit roots deliver complete current kernel and SDK plus canonical public source, never historical verification', t => {
  const f = fixture(t), before = pin(path.join(f.sdk, 'source-manifest.json'));
  archive.stage(f.sdk, f.output, f.bindingFile);
  const manifest = archive.verify(f.output);
  assert.equal(manifest.files.length, 12);
  assert.equal(manifest.releaseReady, false);
  assert.equal(manifest.sourceComposition.currentBinaryProvenance, null);
  assert.equal(manifest.sourceComposition.historicalPrebuiltBinaries, true);
  assert.equal(JSON.stringify(manifest).includes(f.base), false);
  for (const name of f.kernelNames) assert.deepEqual(fs.readFileSync(path.join(f.output, 'third_party/sources/miniblink', name)), fs.readFileSync(path.join(f.kernel, name)));
  assert.equal(fs.readFileSync(path.join(f.output, 'third_party/sources/soui/SOUI/source.cpp'), 'utf8').includes('\r'), false);
  assert.equal(fs.existsSync(path.join(f.output, 'verification')), false);
  assert.deepEqual(pin(path.join(f.sdk, 'source-manifest.json')), before);
  assert.equal(fs.readFileSync(path.join(f.output, 'third_party/prebuilt/x64/node.dll'), 'utf8'), 'historical third_party/prebuilt/x64/node.dll\n');
});
test('explicit create uses the same fresh v2 stage and refuses reuse', t => {
  const f = fixture(t);
  archive.create(f.sdk, {output: f.output, bindingFile: f.bindingFile});
  archive.verify(f.output);
  assert.throws(() => archive.stage(f.sdk, f.output, f.bindingFile), /already exists/);
});
test('missing or equal-length edited current kernel members fail before output creation', t => {
  const f = fixture(t), file = path.join(f.kernel, 'wke/current-model.cpp'), original = fs.readFileSync(file);
  fs.unlinkSync(file);
  assert.throws(() => archive.stage(f.sdk, f.output, f.bindingFile), /file set/);
  fs.writeFileSync(file, Buffer.alloc(original.length, 65));
  assert.throws(() => archive.stage(f.sdk, f.output, f.bindingFile), /modified/);
  assert.equal(fs.existsSync(f.output), false);
});
test('raw roots reject undeclared private output and new source files', t => {
  const f = fixture(t);
  for (const name of ['out/control.json', 'new.cpp']) {
    const file = write(f.kernel, name, 'undeclared');
    assert.throws(() => roots.prepare(f.bindingFile, f.output), /file set/);
    fs.unlinkSync(file);
  }
});
test('manifest pins are authenticated before their data can select source members', t => {
  const f = fixture(t);
  fs.appendFileSync(f.binding.miniblink.manifest.path, ' ');
  assert.throws(() => roots.prepare(f.bindingFile, f.output), /pin differs/);
  assert.equal(fs.existsSync(f.output), false);
});
test('unknown binding fields, wrong version and foreign raw owner are refused', t => {
  const f = fixture(t);
  f.binding.unrestrictedSource = f.base; f.save();
  assert.throws(() => roots.prepare(f.bindingFile, f.output), /fields/);
  delete f.binding.unrestrictedSource; f.save();
  const sdkManifest = id.readJson(f.binding.sdk.manifest.path);
  sdkManifest.root = f.base;
  write(f.base, 'pins/sdk.json', id.encoded(sdkManifest)); f.binding.sdk.manifest = pin(f.binding.sdk.manifest.path); f.save();
  assert.throws(() => roots.prepare(f.bindingFile, f.output), /SDK raw/);
  sdkManifest.root = f.sdk;
  write(f.base, 'pins/sdk.json', id.encoded(sdkManifest)); f.binding.sdk.manifest = pin(f.binding.sdk.manifest.path);
  const publicManifest = id.readJson(f.binding.publicSource.manifest.path); publicManifest.version = 'wrong';
  write(f.base, 'pins/public.json', id.encoded(publicManifest)); f.binding.publicSource.manifest = pin(f.binding.publicSource.manifest.path); f.save();
  assert.throws(() => roots.prepare(f.bindingFile, f.output), /version/);
});
test('missing, modified or undeclared public SOUI source cannot use the stale upstream manifest as fallback', t => {
  const f = fixture(t), file = path.join(f.base, 'third_party/sources/soui/SOUI/source.cpp'), original = fs.readFileSync(file);
  fs.unlinkSync(file);
  assert.throws(() => roots.prepare(f.bindingFile, f.output), /ENOENT|missing/i);
  fs.writeFileSync(file, Buffer.alloc(original.length, 65));
  assert.throws(() => roots.prepare(f.bindingFile, f.output), /modified/);
  fs.writeFileSync(file, original); write(f.base, 'third_party/sources/soui/extra.cpp', 'extra');
  assert.throws(() => roots.prepare(f.bindingFile, f.output), /file set/);
});
test('repository parent locator is allowed but selected source/output overlap is refused', t => {
  const f = fixture(t);
  roots.prepare(f.bindingFile, f.output);
  assert.throws(() => roots.prepare(f.bindingFile, path.join(f.base, 'third_party/sources/soui/new-output')), /overlaps/);
  assert.throws(() => roots.prepare(f.bindingFile, path.join(f.sdk, 'out/stage')), /overlaps/);
  assert.throws(() => roots.prepare(f.bindingFile, f.base), /already exists|overlaps/);
  f.binding.miniblink.root = f.sdk; f.save();
  assert.throws(() => roots.prepare(f.bindingFile, f.output), /overlap/);
});
test('same-root package calls need a fresh external workspace and use the actual SDK version', t => {
  const f = fixture(t);
  const polluted = path.join(f.sdk, 'out/package/source');
  assert.throws(() => roots.prepare(f.bindingFile, polluted), /overlaps/);
  assert.equal(fs.existsSync(path.dirname(polluted)), false);
  const workspace = path.join(f.base, 'out/package-external');
  const stage = path.join(workspace, 'source');
  assert.equal(stage.startsWith(f.sdk), false);
  archive.stage(f.sdk, stage, f.bindingFile);
  assert.equal(archive.verify(stage).version, '0.5.0-preview.1');
  assert.equal(fs.readFileSync(path.join(stage, 'VERSION'), 'utf8').trim(), '0.5.0-preview.1');
  assert.throws(() => roots.prepare(f.bindingFile, stage), /already exists/);
});
test('public raw-source manifests use the same exact raw rules without private composition schemas', t => {
  const f = fixture(t);
  for (const role of ['sdk', 'miniblink']) {
    const entry = f.binding[role];
    const manifest = {schema: 'soui.raw-source/v1', sourceOnly: true, root: entry.root,
      files: rows(entry.root, id.walk(entry.root)).map(({normalization, ...row}) => row)};
    fs.writeFileSync(entry.manifest.path, id.encoded(manifest)); entry.manifest = pin(entry.manifest.path);
  }
  f.save(); archive.stage(f.sdk, f.output, f.bindingFile); archive.verify(f.output);
  const native = id.readJson(f.binding.miniblink.manifest.path); native.sourceOnly = false;
  fs.writeFileSync(f.binding.miniblink.manifest.path, id.encoded(native));
  f.binding.miniblink.manifest = pin(f.binding.miniblink.manifest.path); f.save();
  assert.throws(() => roots.prepare(f.bindingFile, path.join(f.base, 'other-output')), /owner/);
});
test('synchronized private root entries, escaping rows and case-fold duplicates are refused', t => {
  const f = fixture(t), original = id.readJson(f.binding.miniblink.manifest.path);
  for (const name of ['out/control.json', 'OUT/control.json', '.GIT/config', 'Dist/control.json', 'Con/source.cpp', '../outside.cpp', 'WKE/CURRENT-MODEL.CPP']) {
    const manifest = structuredClone(original);
    if (['out/control.json', 'OUT/control.json', '.GIT/config', 'Dist/control.json'].includes(name)) write(f.kernel, name, 'private');
    manifest.files.push({relative: name, bytes: 7, sha256: id.hash(Buffer.from('private'))});
    write(f.base, 'pins/kernel.json', id.encoded(manifest)); f.binding.miniblink.manifest = pin(f.binding.miniblink.manifest.path); f.save();
    assert.throws(() => roots.prepare(f.bindingFile, f.output), /Private|Invalid|duplicate/);
    if (['out/control.json', 'OUT/control.json', '.GIT/config', 'Dist/control.json'].includes(name)) fs.unlinkSync(path.join(f.kernel, name));
  }
});
test('consumed composition ignores build/package caches and re-exports only declared source', t => {
  const f = fixture(t); archive.stage(f.sdk, f.output, f.bindingFile);
  const original = archive.verify(f.output);
  for (const name of ['out/dependencies/x64/build.log', 'out/dependencies/x64/soui-source/generated.cpp',
    'out/package/archive.log', 'dist/soui-sdk-generated-full-source.zip']) write(f.output, name, 'generated cache');
  assert.deepEqual(archive.verify(f.output).files, original.files);
  // This is the existing no-binding Package stage location, below its own out.
  const stage = path.join(f.output, 'out/package/new-source');
  archive.stage(f.output, stage);
  assert.deepEqual(archive.verify(stage).files, original.files);
  assert.equal(fs.existsSync(path.join(stage, 'out')), false);
  assert.equal(fs.existsSync(path.join(stage, 'dist')), false);
  assert.deepEqual(archive.verify(f.output).files, original.files);
  write(f.output, 'third_party/sources/miniblink/wke/undeclared.cpp', 'undeclared source');
  assert.throws(() => archive.verify(f.output), /file set/);
});
test('fresh stages strictly reject an undeclared generated cache injected after manifest creation', t => {
  const f = fixture(t), writeFile = fs.writeFileSync;
  const manifestPath = path.join(f.output, 'source-manifest.json');
  let injected = false;
  fs.writeFileSync = function(file, ...args) {
    const result = writeFile.call(fs, file, ...args);
    if (path.resolve(String(file)) === manifestPath && !injected) {
      injected = true;
      fs.mkdirSync(path.join(f.output, 'out/unexpected'), {recursive: true});
      writeFile.call(fs, path.join(f.output, 'out/unexpected/cache.bin'), Buffer.from('real undeclared file'));
    }
    return result;
  };
  try { assert.throws(() => archive.stage(f.sdk, f.output, f.bindingFile), /file set/); }
  finally { fs.writeFileSync = writeFile; }
  assert.equal(injected, true);
  assert.equal(fs.readFileSync(path.join(f.output, 'out/unexpected/cache.bin'), 'utf8'), 'real undeclared file');
});
test('immutable SourceRoots inputs still reject generated caches while consumed deliveries may contain them', t => {
  const f = fixture(t);
  write(f.sdk, 'out/build/generated.log', 'generated after input freeze');
  assert.throws(() => archive.stage(f.sdk, f.output, f.bindingFile), /file set/);
  assert.equal(fs.existsSync(f.output), false);
});
test('post-copy recheck detects input content and authenticated manifest mutation', t => {
  const f = fixture(t), prepared = roots.prepare(f.bindingFile, f.output);
  const file = path.join(f.kernel, 'wke/current-model.cpp'), original = fs.readFileSync(file);
  fs.writeFileSync(file, Buffer.alloc(original.length, 65));
  assert.throws(() => prepared.recheck(), /modified/);
  fs.writeFileSync(file, original); fs.appendFileSync(f.binding.sdk.manifest.path, ' ');
  assert.throws(() => prepared.recheck(), /pin differs/);
});
test('linked input source is refused without changing the linked target', t => {
  const f = fixture(t), file = path.join(f.kernel, 'wke/current-model.cpp'), outside = write(f.base, 'outside.cpp', fs.readFileSync(file));
  fs.unlinkSync(file);
  try { fs.symlinkSync(outside, file, 'file'); } catch (error) {
    if (['EPERM', 'EACCES', 'ENOTSUP'].includes(error.code)) { t.skip(`symlink capability unavailable: ${error.code}`); return; }
    throw error;
  }
  const before = pin(outside);
  assert.throws(() => roots.prepare(f.bindingFile, f.output), /Linked/);
  assert.deepEqual(pin(outside), before);
});
