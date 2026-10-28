'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const bundle = require('../../tools/internal/runtime-bundle.cjs');
const id = require('../../tools/internal/delivery-identity.cjs');
const cli = path.resolve(__dirname, '../../tools/internal/runtime-bundle.cjs');

function fixture(t) {
  const parent = fs.realpathSync(os.tmpdir()), root = fs.mkdtempSync(path.join(parent, 'soui-runtime-bundle-test-'));
  t.after(() => {
    assert.equal(path.dirname(root), parent);
    assert.ok(path.basename(root).startsWith('soui-runtime-bundle-test-'));
    id.safePath(root);
    fs.rmSync(root, {recursive: true, force: true});
  });
  const put = (relative, content) => {
    const file = id.safePath(root, relative);
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, content);
    return file;
  };
  function application(name = 'Main', architecture = 'x64', kernel = 'shared kernel') {
    put(`${name}/${name}.exe`, `exe ${name} ${architecture}`);
    put(`${name}/node.dll`, kernel);
    put(`${name}/shared.dll`, 'shared native library');
    put(`${name}/resources/index.html`, '<html>same interface</html>');
    const directory = path.join(root, name);
    const report = {schema: 'soui.application-size/v1', version: '0.5.0', target: name,
      architecture, configuration: 'Release', rendererBackend: 'miniblink', buildId: `build-${name}`,
      sourceSha256: 'a'.repeat(64), releaseReady: false, categories: {web: 27},
      payload: id.snapshot(directory, id.walk(directory))};
    return {name, root: directory, report};
  }
  const output = path.join(root, 'packed'), destination = path.join(root, 'restored');
  const read = () => id.readJson(path.join(output, 'manifest.json'));
  const save = manifest => fs.writeFileSync(path.join(output, 'manifest.json'), id.encoded(manifest));
  return {root, put, application, output, destination, read, save};
}
function refreshPayload(report) {
  report.payload.bytes = report.payload.files.reduce((sum, file) => sum + file.bytes, 0);
  report.payload.sha256 = id.hash(id.encoded(report.payload.files));
}
function noStages(root) {
  assert.equal(fs.readdirSync(root).filter(name => name.startsWith('.soui-bundle-')).length, 0);
}

test('deduplicates identical bytes across applications and restores independent ordinary files', t => {
  const f = fixture(t), apps = [f.application('Main'), f.application('Uninstall')];
  const result = bundle.pack(f.output, apps);
  assert.equal(result.fileCount, 8);
  assert.equal(result.blobCount, 5);
  assert.equal(result.savedUncompressedBytes, Buffer.byteLength('shared kernelshared native library<html>same interface</html>'));
  assert.equal(result.logicalBytes - result.uniqueBytes, result.savedUncompressedBytes);
  const manifest = f.read();
  assert.deepEqual(manifest.applications.map(app => app.report), apps.map(app => app.report));
  assert.deepEqual(bundle.verify(f.output).applications.map(app => app.name), ['Main', 'Uninstall']);
  const restored = bundle.restore(f.output, f.destination);
  assert.equal(restored.restored, true);
  for (const app of apps) for (const file of app.report.payload.files) {
    const target = path.join(f.destination, app.name, file.path);
    assert.equal(id.hash(fs.readFileSync(target)), file.sha256);
    assert.equal(fs.statSync(target).nlink, 1);
  }
  fs.writeFileSync(path.join(f.destination, 'Main/node.dll'), 'independent');
  assert.equal(fs.readFileSync(path.join(f.destination, 'Uninstall/node.dll'), 'utf8'), 'shared kernel');
  assert.equal(bundle.verify(f.output).verified, true);
  noStages(f.root);
});

test('same node.dll name with different content remains separate and architecture identity is retained', t => {
  const f = fixture(t), apps = [f.application('Main', 'x64', 'kernel 64'), f.application('Pe', 'x86', 'kernel 32')];
  bundle.pack(f.output, apps);
  const manifest = f.read(), hashes = apps.map(app => app.report.payload.files.find(file => file.path === 'node.dll').sha256);
  assert.notEqual(hashes[0], hashes[1]);
  assert.ok(hashes.every(hash => manifest.blobs.some(blob => blob.sha256 === hash)));
  assert.deepEqual(bundle.verify(f.output).applications.map(app => app.architecture), ['x64', 'x86']);
  bundle.restore(f.output, f.destination);
  assert.equal(fs.readFileSync(path.join(f.destination, 'Main/node.dll'), 'utf8'), 'kernel 64');
  assert.equal(fs.readFileSync(path.join(f.destination, 'Pe/node.dll'), 'utf8'), 'kernel 32');
});

test('portable bundle restores after all original application directories have moved away', t => {
  const f = fixture(t), app = f.application();
  bundle.pack(f.output, [app]);
  fs.renameSync(app.root, path.join(f.root, 'source-retired'));
  const relocated = path.join(f.root, 'moved-bundle');
  fs.renameSync(f.output, relocated);
  bundle.restore(relocated, f.destination);
  assert.equal(fs.readFileSync(path.join(f.destination, 'Main/node.dll'), 'utf8'), 'shared kernel');
});

for (const fault of ['missing', 'extra', 'corrupt', 'extra-directory', 'corrupt-json', 'totals', 'schema', 'payload', 'blob-size', 'metadata']) {
  test(`rejects ${fault} bundle and leaves no restore destination`, t => {
    const f = fixture(t);
    bundle.pack(f.output, [f.application()]);
    const manifest = f.read(), blob = path.join(f.output, 'blobs', manifest.blobs[0].sha256);
    if (fault === 'missing') fs.unlinkSync(blob);
    if (fault === 'extra') fs.writeFileSync(path.join(f.output, 'blobs', 'f'.repeat(64)), 'extra');
    if (fault === 'corrupt') fs.writeFileSync(blob, 'corrupted');
    if (fault === 'extra-directory') fs.mkdirSync(path.join(f.output, 'unused'));
    if (fault === 'corrupt-json') fs.writeFileSync(path.join(f.output, 'manifest.json'), '{');
    if (fault === 'totals') { manifest.totals.savedUncompressedBytes++; f.save(manifest); }
    if (fault === 'schema') { manifest.schema = 'soui.runtime-bundle/v999'; f.save(manifest); }
    if (fault === 'payload') { manifest.applications[0].report.payload.sha256 = 'b'.repeat(64); f.save(manifest); }
    if (fault === 'blob-size') { manifest.blobs[0].bytes++; f.save(manifest); }
    if (fault === 'metadata') { manifest.applications[0].report.architecture = 'arm64'; f.save(manifest); }
    assert.throws(() => bundle.verify(f.output));
    assert.throws(() => bundle.restore(f.output, f.destination));
    assert.equal(fs.existsSync(f.destination), false);
    noStages(f.root);
  });
}

for (const invalid of ['../escape.dll', '/absolute.dll', 'C:/drive.dll', 'resources\\bad.dll', 'resources/CON.txt', 'resources/trailing.']) {
  test(`rejects unsafe logical path ${invalid}`, t => {
    const f = fixture(t), app = f.application();
    const file = app.report.payload.files.find(entry => entry.path === 'shared.dll');
    file.path = invalid;
    refreshPayload(app.report);
    assert.throws(() => bundle.pack(f.output, [app]), /path/i);
    assert.equal(fs.existsSync(f.output), false);
  });
}

test('rejects duplicate application names ignoring case before creating output', t => {
  const f = fixture(t), first = f.application(), other = {...first, name: 'main'};
  assert.throws(() => bundle.pack(f.output, [first, other]), /application/i);
  assert.equal(fs.existsSync(f.output), false);
});

for (const fault of ['case-file', 'prefix', 'case-directory', 'bad-size', 'bad-hash', 'missing-exe', 'conflicting-hash-size']) {
  test(`rejects ${fault} source report`, t => {
    const f = fixture(t), app = f.application(), files = app.report.payload.files;
    if (fault === 'case-file') files.push({...files.find(file => file.path === 'node.dll'), path: 'NODE.dll'});
    if (fault === 'prefix') files.push({path: 'resources', bytes: 0, sha256: id.hash('')});
    if (fault === 'case-directory') files.push({path: 'Resources/a', bytes: 0, sha256: id.hash('')});
    if (fault === 'bad-size') files[0].bytes = Number.MAX_SAFE_INTEGER;
    if (fault === 'bad-hash') files[0].sha256 = 'a'.repeat(63);
    if (fault === 'missing-exe') files.splice(files.findIndex(file => file.path === 'Main.exe'), 1);
    if (fault === 'conflicting-hash-size') files.push({...files[0], path: 'other.dll', bytes: files[0].bytes + 1});
    refreshPayload(app.report);
    assert.throws(() => bundle.pack(f.output, [app]));
    assert.equal(fs.existsSync(f.output), false);
  });
}

test('rejects stale report or extra application file', t => {
  const f = fixture(t), app = f.application();
  fs.writeFileSync(path.join(app.root, 'node.dll'), 'changed source');
  assert.throws(() => bundle.pack(f.output, [app]), /differs/);
  fs.writeFileSync(path.join(app.root, 'node.dll'), 'shared kernel');
  fs.writeFileSync(path.join(app.root, 'foreign.dll'), 'not declared');
  assert.throws(() => bundle.pack(f.output, [app]), /inventory/);
  assert.equal(fs.existsSync(f.output), false);
});

test('source drift during copying rejects publication and cleans owned stage', t => {
  const f = fixture(t), app = f.application(), originalCopy = fs.copyFileSync;
  let changed = false;
  fs.copyFileSync = function(from, to, flags) {
    originalCopy(from, to, flags);
    if (!changed) { changed = true; fs.writeFileSync(path.join(app.root, 'node.dll'), 'drift'); }
  };
  try { assert.throws(() => bundle.pack(f.output, [app]), /differs/); }
  finally { fs.copyFileSync = originalCopy; }
  assert.equal(fs.existsSync(f.output), false);
  noStages(f.root);
});

test('restore readback failure never publishes partially restored applications', t => {
  const f = fixture(t);
  bundle.pack(f.output, [f.application()]);
  const originalCopy = fs.copyFileSync;
  fs.copyFileSync = function(from, to, flags) { originalCopy(from, to, flags); fs.appendFileSync(to, 'bad'); };
  try { assert.throws(() => bundle.restore(f.output, f.destination), /differs/); }
  finally { fs.copyFileSync = originalCopy; }
  assert.equal(fs.existsSync(f.destination), false);
  noStages(f.root);
});

test('existing output and restore destinations preserve all user contents', t => {
  const f = fixture(t), app = f.application();
  bundle.pack(f.output, [app]);
  const manifest = fs.readFileSync(path.join(f.output, 'manifest.json'));
  assert.throws(() => bundle.pack(f.output, [app]), /already exists/);
  assert.deepEqual(fs.readFileSync(path.join(f.output, 'manifest.json')), manifest);
  f.put('restored/user.txt', 'must survive');
  assert.throws(() => bundle.restore(f.output, f.destination), /already exists/);
  assert.equal(fs.readFileSync(path.join(f.destination, 'user.txt'), 'utf8'), 'must survive');
});

test('rejects output nested in source or restore output nested in bundle', t => {
  const f = fixture(t), app = f.application();
  assert.throws(() => bundle.pack(path.join(app.root, 'pack'), [app]), /outside/);
  bundle.pack(f.output, [app]);
  assert.throws(() => bundle.restore(f.output, path.join(f.output, 'restore')), /outside/);
});

test('rejects linked input, bundle content and output parent without traversing them', t => {
  const f = fixture(t), app = f.application();
  const linked = path.join(f.root, 'linked');
  fs.symlinkSync(app.root, linked, process.platform === 'win32' ? 'junction' : 'dir');
  try {
    assert.throws(() => bundle.pack(f.output, [{...app, root: linked}]), /[Ll]inked/);
    assert.throws(() => bundle.pack(path.join(linked, 'bundle'), [app]), /[Ll]inked/);
    bundle.pack(f.output, [app]);
    assert.throws(() => bundle.restore(f.output, path.join(linked, 'restored')), /[Ll]inked/);
    const blobDir = path.join(f.output, 'blobs'), moved = path.join(f.root, 'original-blobs');
    fs.renameSync(blobDir, moved);
    fs.symlinkSync(moved, blobDir, process.platform === 'win32' ? 'junction' : 'dir');
    try { assert.throws(() => bundle.verify(f.output), /[Ll]inked/); }
    finally { fs.unlinkSync(blobDir); }
  } finally { fs.unlinkSync(linked); }
});

test('rejects hard-linked application files', t => {
  const f = fixture(t), app = f.application(), kernel = path.join(app.root, 'node.dll');
  fs.linkSync(kernel, path.join(f.root, 'linked-kernel'));
  assert.throws(() => bundle.pack(f.output, [app]), /[Ll]inked|ordinary/);
});

test('CLI supports relative source/report files and portable verify/restore', t => {
  const f = fixture(t), app = f.application();
  f.put('report.json', id.encoded(app.report));
  const config = f.put('applications.json', id.encoded([{name: app.name, root: 'Main', reportFile: 'report.json'}]));
  const run = args => spawnSync(process.execPath, [cli, ...args], {encoding: 'utf8'});
  const packed = run(['pack', '--output', f.output, '--applications', config]);
  assert.equal(packed.status, 0, packed.stderr);
  assert.equal(JSON.parse(packed.stdout).verified, true);
  const verified = run(['verify', '--bundle', f.output]);
  assert.equal(verified.status, 0, verified.stderr);
  const restored = run(['restore', '--bundle', f.output, '--output', f.destination]);
  assert.equal(restored.status, 0, restored.stderr);
  assert.equal(JSON.parse(restored.stdout).restored, true);
  assert.notEqual(run(['verify', '--bundle', f.output, '--output', f.destination]).status, 0);
});

test('rejects oversized JSON input before parsing or publishing', t => {
  const f = fixture(t), config = f.put('oversized.json', '[]');
  fs.truncateSync(config, 32 * 1024 * 1024 + 1);
  const result = spawnSync(process.execPath, [cli, 'pack', '--output', f.output, '--applications', config], {encoding: 'utf8'});
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /budget/);
  assert.equal(fs.existsSync(f.output), false);
});
