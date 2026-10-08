'use strict';
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const sdk = require('../../tools/sdk/soui-sdk.cjs');
const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-sdk-smoke-'));
const source = path.join(root, 'source');
const first = path.join(root, 'sdk1');
const second = path.join(root, 'sdk2');
const project = path.join(root, 'business');
const write = (name, value) => { fs.mkdirSync(path.dirname(name), {recursive: true}); fs.writeFileSync(name, value); };
const read = name => fs.readFileSync(name, 'utf8');
const packageJson = name => JSON.parse(read(path.join(name, 'sdk-manifest.json')));
const saveManifest = (name, value) => write(path.join(name, 'sdk-manifest.json'), JSON.stringify(value, null, 2) + '\n');
const fileHash = name => crypto.createHash('sha256').update(fs.readFileSync(name)).digest('hex');
let checks = 0;
function check(fn) { fn(); ++checks; }
try {
  for (const relative of sdk.managedRoots) write(path.join(source, relative, 'fixture.txt'), `SDK ${relative}\n`);
  for (const relative of sdk.managedFiles) write(path.join(source, relative), 'SDK notice\n');
  write(path.join(source, 'web/apps/business.tsx'), 'template business must not enter SDK');
  check(() => {
    const localPackage = path.join(source, 'dist/sdk/0.1.0-preview.1');
    sdk.pack(source, localPackage, '0.1.0-preview.1');
    assert.equal(sdk.readPackage(localPackage).files.some(file => file.path.startsWith('dist/')), false);
    assert.throws(() => sdk.pack(source, path.join(source, 'sdk/nested'), '0.1.0-preview.1'), /outside source/);
  });
  check(() => {
    sdk.pack(source, first, '0.1.0-preview.1');
    assert.equal(sdk.readPackage(first).releaseReady, false);
    assert.equal(read(path.join(first, 'payload/THIRD_PARTY_NOTICES.md')), 'SDK notice\n');
    assert.equal(read(path.join(first, 'payload/third_party/licenses/fixture.txt')), 'SDK third_party/licenses\n');
    assert.equal(fs.existsSync(path.join(first, 'payload/web/apps/business.tsx')), false);
    assert.throws(() => sdk.pack(source, first, '0.1.0-preview.1'), /already exists/);
  });
  if (process.platform === 'win32') check(() => {
    const transient = path.join(root, 'sdk-transient');
    const denied = path.join(root, 'sdk-denied');
    const rename = fs.renameSync;
    let injected = 0;
    fs.renameSync = (from, to) => {
      if (to === transient && injected < 2) {
        ++injected;
        const error = new Error('temporary Windows rename denial');
        error.code = 'EPERM';
        throw error;
      }
      return rename(from, to);
    };
    try { sdk.pack(source, transient, '0.1.0-preview.1'); }
    finally { fs.renameSync = rename; }
    assert.equal(injected, 2);
    sdk.readPackage(transient);

    let attempts = 0;
    fs.renameSync = (from, to) => {
      if (to === denied) {
        ++attempts;
        const error = new Error('persistent Windows rename denial');
        error.code = 'EACCES';
        throw error;
      }
      return rename(from, to);
    };
    try { assert.throws(() => sdk.pack(source, denied, '0.1.0-preview.1'), {code: 'EACCES'}); }
    finally { fs.renameSync = rename; }
    assert.equal(attempts, 6);
    assert.equal(fs.existsSync(denied), false);
    assert.equal(fs.readdirSync(root).some(name => name.startsWith('sdk-denied.staging-')), false);
  });
  check(() => {
    write(path.join(project, 'web/apps/business.tsx'), 'business custom');
    sdk.install(first, project);
    assert.equal(sdk.readLock(project).manifest.version, '0.1.0-preview.1');
    assert.equal(read(path.join(project, 'THIRD_PARTY_NOTICES.md')), 'SDK notice\n');
    assert.equal(read(path.join(project, 'third_party/licenses/fixture.txt')), 'SDK third_party/licenses\n');
    assert.equal(read(path.join(project, 'web/apps/business.tsx')), 'business custom');
  });
  const touchedPath = path.join(project, 'sdk/fixture.txt');
  check(() => {
    const original = read(touchedPath);
    write(touchedPath, 'local SDK edit');
    assert.throws(() => sdk.readLock(project), /modified/);
    assert.throws(() => sdk.install(first, project), /modified/);
    assert.equal(read(touchedPath), 'local SDK edit');
    write(touchedPath, original);
  });
  check(() => {
    const collision = path.join(root, 'collision');
    write(path.join(collision, 'sdk/fixture.txt'), 'business owned');
    assert.throws(() => sdk.install(first, collision), /collision/);
    assert.equal(read(path.join(collision, 'sdk/fixture.txt')), 'business owned');
    assert.equal(fs.existsSync(path.join(collision, 'sdk-lock.json')), false);
  });
  check(() => {
    const payload = path.join(first, 'payload/sdk/fixture.txt');
    const original = read(payload);
    write(payload, 'corrupted transport');
    assert.throws(() => sdk.install(first, project), /modified/);
    write(payload, original);
    sdk.readLock(project);
  });
  check(() => {
    const original = packageJson(first);
    for (const malicious of ['../outside.txt', 'sdk/../../web/apps/business.tsx', 'web/apps/business.tsx', 'sdk/NUL.txt', 'sdk/a:stream']) {
      saveManifest(first, {...original, files: [{...original.files[0], path: malicious}]});
      assert.throws(() => sdk.install(first, project), /Invalid/);
    }
    saveManifest(first, original);
  });
  write(path.join(source, 'sdk/fixture.txt'), 'SDK version two');
  write(path.join(source, 'sdk/new.txt'), 'new API');
  fs.unlinkSync(path.join(source, 'cmake/fixture.txt'));
  sdk.pack(source, second, '0.1.0-preview.2');
  check(() => {
    const original = packageJson(second);
    saveManifest(second, {...original, version: '0.1.0-preview.1'});
    assert.throws(() => sdk.install(second, project), /different contents/);
    saveManifest(second, original);
  });
  check(() => {
    const oldLock = read(path.join(project, 'sdk-lock.json'));
    const oldCore = read(touchedPath);
    const rename = fs.renameSync;
    let injected = false;
    fs.renameSync = (from, to) => {
      if (!injected && from.includes(`${path.sep}new${path.sep}sdk${path.sep}`)) {
        injected = true;
        throw new Error('simulated installation IO failure');
      }
      return rename(from, to);
    };
    try { assert.throws(() => sdk.install(second, project), /simulated/); }
    finally { fs.renameSync = rename; }
    assert.equal(injected, true);
    assert.equal(read(path.join(project, 'sdk-lock.json')), oldLock);
    assert.equal(read(touchedPath), oldCore);
    sdk.readLock(project);
  });
  check(() => {
    sdk.install(second, project);
    assert.equal(read(touchedPath), 'SDK version two');
    assert.equal(read(path.join(project, 'sdk/new.txt')), 'new API');
    assert.equal(fs.existsSync(path.join(project, 'cmake/fixture.txt')), false);
    assert.equal(read(path.join(project, 'web/apps/business.tsx')), 'business custom');
    assert.equal(sdk.readLock(project).manifest.version, '0.1.0-preview.2');
  });
  check(() => {
    sdk.install(first, project);
    assert.equal(sdk.readLock(project).manifest.version, '0.1.0-preview.1');
    assert.equal(fs.existsSync(path.join(project, 'sdk/new.txt')), false);
    assert.equal(read(path.join(project, 'web/apps/business.tsx')), 'business custom');
  });
  check(() => {
    const duplicate = path.join(root, 'same-source-package');
    sdk.pack(source, duplicate, '0.1.0-preview.2');
    assert.equal(fileHash(path.join(duplicate, 'sdk-manifest.json')), fileHash(path.join(second, 'sdk-manifest.json')));
  });
  check(() => {
    const outside = path.join(root, 'outside');
    const linked = path.join(root, 'linked-project');
    fs.mkdirSync(outside);
    fs.mkdirSync(linked);
    fs.symlinkSync(outside, path.join(linked, 'sdk'), process.platform === 'win32' ? 'junction' : 'dir');
    assert.throws(() => sdk.install(first, linked), /Linked path/);
    assert.deepEqual(fs.readdirSync(outside), []);
  });
  console.log(`SDK package smoke: ${checks}/${checks} passed (ownership, integrity, upgrade, rollback, IO recovery and paths).`);
} finally {
  const expected = path.resolve(os.tmpdir()) + path.sep;
  if (!path.resolve(root).startsWith(expected) || !path.basename(root).startsWith('soui-sdk-smoke-')) throw new Error('Unsafe cleanup');
  fs.rmSync(root, {recursive: true, force: true});
}
