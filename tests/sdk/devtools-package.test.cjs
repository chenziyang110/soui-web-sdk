'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const crypto = require('node:crypto');
const devtools = require('../../tools/internal/devtools-package.cjs');

const MiB = 1024 * 1024;
const sha256 = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const json = value => JSON.stringify(value, null, 2) + '\n';

function syntheticPe(architecture) {
  // This fixture is data only. No DLL is loaded or executed by these tests.
  const bytes = Buffer.alloc(512);
  bytes.write('MZ', 0, 'ascii');
  bytes.writeUInt32LE(0x80, 0x3c);
  bytes.write('PE\0\0', 0x80, 'ascii');
  bytes.writeUInt16LE(architecture === 'x64' ? 0x8664 : 0x14c, 0x84);
  return bytes;
}

function inventory(root) {
  if (!fs.existsSync(root)) return [];
  const files = [];
  function visit(directory, relative = '') {
    for (const name of fs.readdirSync(directory).sort()) {
      const member = relative ? relative + '/' + name : name;
      const file = path.join(directory, name);
      const stat = fs.lstatSync(file);
      assert.equal(stat.isSymbolicLink(), false, 'A test snapshot must never follow links');
      if (stat.isDirectory()) visit(file, member);
      else files.push({path: member, bytes: stat.size, sha256: sha256(fs.readFileSync(file))});
    }
  }
  visit(root);
  return files;
}

function fixture(t, architecture = 'x64') {
  const owner = fs.realpathSync(os.tmpdir());
  const base = fs.realpathSync(fs.mkdtempSync(path.join(owner, '.soui-devtools-package-test-')));
  t.after(() => {
    assert.equal(path.dirname(base), owner, 'Cleanup is limited to this directly created temporary fixture');
    assert.ok(path.basename(base).startsWith('.soui-devtools-package-test-'));
    assert.equal(require('../../tools/internal/delivery-identity.cjs').safePath(base), base);
    fs.rmSync(base, {recursive: true, force: true});
  });
  const frontendRoot = path.join(base, 'input');
  const output = path.join(base, 'prepared');
  const applicationOutput = path.join(base, 'application');
  const kernelRelative = 'kernel/node.dll';
  const approvalRelative = 'package-approval.json';
  const sdkVersion = '0.5.0';
  function write(file, bytes) {
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, bytes);
    return file;
  }
  const source = (relative, bytes) => write(path.join(frontendRoot, relative), bytes);
  source('front_end/inspector.html', '<!doctype html><script src="inspector.js"></script>\n');
  source('front_end/inspector.js', 'window.syntheticInspector = true;\n');
  source(kernelRelative, syntheticPe(architecture));
  source('licenses/LICENSE.txt', 'Synthetic fixture license; no third-party material.\n');
  const fileEntry = relative => {
    const bytes = fs.readFileSync(path.join(frontendRoot, relative));
    return {path: relative, bytes: bytes.length, sha256: sha256(bytes)};
  };
  const f = {
    base, frontendRoot, output, applicationOutput, architecture, kernelRelative, approvalRelative,
    sdkVersion, write, source, fileEntry,
    packageRoot: path.join(output, 'package'),
    deployedRoot: path.join(applicationOutput, 'devtools', 'package'),
    entries: ['front_end/inspector.html', 'front_end/inspector.js'].map(fileEntry),
    kernelSha256: sha256(fs.readFileSync(path.join(frontendRoot, kernelRelative))),
    approval: {
      schema: 'soui.devtools-package-approval/v1',
      backend: 'miniblink',
      kernel: {path: kernelRelative, sha256: '', version: 'synthetic-1'},
      frontend: {manifest: 'approved-manifest.json', sha256: '', version: 'synthetic-1'},
      licenses: [fileEntry('licenses/LICENSE.txt')],
    },
    saveApproval() {
      const bytes = json(f.approval);
      source(approvalRelative, bytes);
      f.approvalSha256 = sha256(bytes);
    },
    saveManifest(entries = f.entries) {
      f.entries = entries;
      const bytes = json(entries);
      source('approved-manifest.json', bytes);
      f.manifestSha256 = sha256(bytes);
      f.approval.frontend.sha256 = f.manifestSha256;
      f.saveApproval();
    },
    prepareOptions(extra = {}) {
      return {
        frontendRoot, kernelRelative, kernelSha256: f.kernelSha256,
        manifestSha256: f.manifestSha256, approvalRelative, approvalSha256: f.approvalSha256,
        architecture, sdkVersion, output, ...extra,
      };
    },
    deployOptions(extra = {}) {
      return {
        preparedRoot: output, applicationOutput, kernelSha256: f.kernelSha256,
        manifestSha256: f.manifestSha256, approvalSha256: f.approvalSha256,
        architecture, sdkVersion, ...extra,
      };
    },
    prepare(extra) { return devtools.prepare(f.prepareOptions(extra)); },
    deploy(extra) { return devtools.deploy(f.deployOptions(extra)); },
    application(relative, bytes) { return write(path.join(applicationOutput, relative), bytes); },
    installKernel() {
      return f.application('node.dll', fs.readFileSync(path.join(frontendRoot, kernelRelative)));
    },
  };
  f.approval.kernel.sha256 = f.kernelSha256;
  f.saveManifest();
  return f;
}

async function rejectsDevtools(action) {
  await assert.rejects(async () => action(), error => {
    assert.match(String(error.code || error.message), /E_DEVTOOLS_[A-Z_]+/,
      'Boundary failures must carry a stable DevTools error code');
    return true;
  });
}

async function rejectsPrepare(f, extra) {
  await rejectsDevtools(() => f.prepare(extra));
  assert.equal(fs.existsSync(f.packageRoot), false, 'Rejected input must not publish a prepared package');
  assert.equal(fs.existsSync(path.join(f.output, 'soui_devtools_package.h')), false,
    'Rejected input must not publish a native package header');
}

function sparse(file, bytes) {
  fs.mkdirSync(path.dirname(file), {recursive: true});
  const descriptor = fs.openSync(file, 'w');
  try { fs.ftruncateSync(descriptor, bytes); }
  finally { fs.closeSync(descriptor); }
}

function linkOrSkip(t, target, link, type = 'file') {
  try { fs.symlinkSync(target, link, type); }
  catch (error) {
    if (['EPERM', 'EACCES', 'ENOSYS'].includes(error.code)) {
      t.skip('The platform does not permit creating this fixture symlink.');
      return false;
    }
    throw error;
  }
  return true;
}

test('the package tool exposes explicit preparation and deployment entry points', () => {
  assert.equal(typeof devtools.prepare, 'function');
  assert.equal(typeof devtools.deploy, 'function');
  assert.equal(typeof devtools.limits, 'object');
});

for (const architecture of ['x86', 'x64']) {
  test('real prepare and deploy preserve exact synthetic bytes for ' + architecture, async t => {
    const f = fixture(t, architecture);
    await f.prepare();
    const preparedFiles = inventory(f.packageRoot).map(entry => entry.path);
    assert.deepEqual(preparedFiles, [
      'approval.json', 'frontend/approved-manifest.json', 'frontend/front_end/inspector.html',
      'frontend/front_end/inspector.js', 'kernel/node.dll', 'legal/licenses/LICENSE.txt', 'package.json',
    ]);
    for (const [source, destination] of [
      ['approved-manifest.json', 'frontend/approved-manifest.json'],
      ['front_end/inspector.html', 'frontend/front_end/inspector.html'],
      ['front_end/inspector.js', 'frontend/front_end/inspector.js'],
      [f.kernelRelative, 'kernel/node.dll'],
      ['licenses/LICENSE.txt', 'legal/licenses/LICENSE.txt'],
      [f.approvalRelative, 'approval.json'],
    ]) {
      assert.deepEqual(fs.readFileSync(path.join(f.packageRoot, destination)),
        fs.readFileSync(path.join(f.frontendRoot, source)));
    }
    const header = fs.readFileSync(path.join(f.output, 'soui_devtools_package.h'), 'utf8');
    const metadata = fs.readFileSync(path.join(f.packageRoot, 'package.json'), 'utf8');
    assert.doesNotThrow(() => JSON.parse(metadata));
    for (const identity of [f.kernelSha256, f.manifestSha256, f.approvalSha256, architecture, f.sdkVersion])
      assert.ok((header + metadata).includes(identity), 'Generated artifacts must bind ' + identity);
    f.installKernel();
    const userFile = f.application('userdata/notes.txt', 'Application-owned state.\n');
    const unrelated = f.application('devtools/application-settings.json', '{"theme":"dark"}\n');
    await f.deploy();
    assert.deepEqual(inventory(f.deployedRoot).filter(entry => entry.path !== 'package.json'),
      inventory(f.packageRoot).filter(entry =>
        !['approval.json', 'kernel/node.dll', 'package.json'].includes(entry.path)));
    const deliveredMetadata = JSON.parse(fs.readFileSync(path.join(f.deployedRoot, 'package.json'), 'utf8'));
    assert.equal(deliveredMetadata.mode, 'delivery');
    assert.equal(deliveredMetadata.architecture, architecture);
    assert.equal(deliveredMetadata.kernel.sha256, f.kernelSha256);
    assert.equal(deliveredMetadata.frontend.manifest_sha256, f.manifestSha256);
    assert.equal(deliveredMetadata.approval_sha256, f.approvalSha256);
    assert.equal(fs.readFileSync(userFile, 'utf8'), 'Application-owned state.\n');
    assert.equal(fs.readFileSync(unrelated, 'utf8'), '{"theme":"dark"}\n');
    assert.equal(sha256(fs.readFileSync(path.join(f.applicationOutput, 'node.dll'))), f.kernelSha256);
  });
}

for (const field of ['frontendRoot', 'kernelRelative', 'kernelSha256', 'manifestSha256',
  'approvalRelative', 'approvalSha256', 'architecture', 'sdkVersion', 'output']) {
  test('prepare requires an explicit ' + field, async t => {
    const f = fixture(t);
    await rejectsPrepare(f, {[field]: undefined});
  });
}

for (const field of ['kernelSha256', 'manifestSha256', 'approvalSha256']) {
  test('prepare rejects an external ' + field + ' mismatch before writing', async t => {
    const f = fixture(t);
    await rejectsPrepare(f, {[field]: '0'.repeat(64)});
    assert.equal(fs.existsSync(f.output), false, 'A rejected external pin must not create preparation output');
  });
}

for (const [name, change] of [
  ['kernel identity', f => { f.approval.kernel.sha256 = '0'.repeat(64); }],
  ['kernel path', f => { f.approval.kernel.path = 'kernel/other.dll'; }],
  ['frontend identity', f => { f.approval.frontend.sha256 = '0'.repeat(64); }],
  ['frontend manifest path', f => { f.approval.frontend.manifest = 'other-manifest.json'; }],
  ['backend', f => { f.approval.backend = 'webview2'; }],
  ['schema', f => { f.approval.schema = 'unapproved/v1'; }],
  ['empty licenses', f => { f.approval.licenses = []; }],
]) {
  test('a correctly pinned approval cannot authorize a mismatched ' + name, async t => {
    const f = fixture(t);
    change(f);
    f.saveApproval();
    await rejectsPrepare(f);
  });
}

for (const relative of ['front_end/unapproved.js', 'licenses/unapproved.txt', 'kernel/other.dll']) {
  test('prepare rejects unapproved input member ' + relative, async t => {
    const f = fixture(t);
    f.source(relative, 'Unapproved synthetic bytes.\n');
    await rejectsPrepare(f);
  });
}

test('the manifest must remain a bare array and include the inspector entry', async t => {
  for (const entries of [{files: []}, []]) {
    const f = fixture(t);
    f.saveManifest(entries);
    await rejectsPrepare(f);
  }
});

for (const [name, change] of [
  ['recorded size', f => { f.entries[0].bytes += 1; }],
  ['recorded digest', f => { f.entries[0].sha256 = '0'.repeat(64); }],
  ['negative size', f => { f.entries[0].bytes = -1; }],
  ['nonintegral size', f => { f.entries[0].bytes = 0.5; }],
  ['duplicate path', f => { f.entries.push({...f.entries[0]}); }],
  ['case alias', f => { f.entries.push({...f.entries[0], path: 'FRONT_END/INSPECTOR.HTML'}); }],
]) {
  test('prepare rejects manifest ' + name, async t => {
    const f = fixture(t);
    change(f);
    f.saveManifest();
    await rejectsPrepare(f);
  });
}

for (const relative of ['../outside.html', '/absolute.html', 'C:/outside.html',
  'front_end\\inspector.html', 'front_end/../inspector.html', 'front_end/CON.html',
  'front_end/inspector.html:stream', 'front_end/trailing.']) {
  test('prepare rejects unsafe manifest path ' + relative, async t => {
    const f = fixture(t);
    f.entries.push({...f.entries[0], path: relative});
    f.saveManifest();
    await rejectsPrepare(f);
  });
}

test('changed license bytes cannot be authorized by the frontend manifest pin', async t => {
  const f = fixture(t);
  f.source('licenses/LICENSE.txt', 'Changed synthetic license.\n');
  await rejectsPrepare(f);
});

test('a license cannot alias a frontend member by case', async t => {
  const f = fixture(t);
  f.approval.licenses = [{...f.entries[0], path: 'FRONT_END/INSPECTOR.HTML'}];
  f.saveApproval();
  await rejectsPrepare(f);
});

for (const architecture of ['x86', 'x64']) {
  test('PE header architecture must match the explicit ' + architecture + ' selection', async t => {
    const f = fixture(t, architecture);
    await rejectsPrepare(f, {architecture: architecture === 'x64' ? 'x86' : 'x64'});
  });
}

test('a pinned non-PE kernel is rejected without loading it', async t => {
  const f = fixture(t);
  const bytes = Buffer.from('This is not a DLL.\n');
  f.source(f.kernelRelative, bytes);
  f.kernelSha256 = sha256(bytes);
  f.approval.kernel.sha256 = f.kernelSha256;
  f.saveApproval();
  await rejectsPrepare(f);
});

test('frontend symlinks are rejected even when their target is inside input', async t => {
  const f = fixture(t);
  const linked = path.join(f.frontendRoot, 'front_end', 'linked.js');
  if (!linkOrSkip(t, path.join(f.frontendRoot, 'front_end', 'inspector.js'), linked)) return;
  f.entries.push({...f.entries[1], path: 'front_end/linked.js'});
  f.saveManifest();
  await rejectsPrepare(f);
});

test('input directory links cannot introduce an external approved member', async t => {
  const f = fixture(t);
  const external = path.join(f.base, 'external');
  f.write(path.join(external, 'external.js'), 'window.external = true;\n');
  if (!linkOrSkip(t, external, path.join(f.frontendRoot, 'linked'), 'junction')) return;
  const bytes = fs.readFileSync(path.join(external, 'external.js'));
  f.entries.push({path: 'linked/external.js', bytes: bytes.length, sha256: sha256(bytes)});
  f.saveManifest();
  await rejectsPrepare(f);
});

test('frontend hard links are rejected even with an exact manifest identity', async t => {
  const f = fixture(t);
  fs.linkSync(path.join(f.frontendRoot, 'front_end', 'inspector.js'),
    path.join(f.frontendRoot, 'front_end', 'linked.js'));
  f.entries.push({...f.entries[1], path: 'front_end/linked.js'});
  f.saveManifest();
  await rejectsPrepare(f);
});

test('individual frontend resources are limited to 1 MiB', async t => {
  const f = fixture(t);
  const relative = 'front_end/oversized.bin';
  sparse(path.join(f.frontendRoot, relative), MiB + 1);
  f.entries.push(f.fileEntry(relative));
  f.saveManifest();
  await rejectsPrepare(f);
});

test('aggregate frontend resources are limited to 16 MiB', async t => {
  const f = fixture(t);
  for (let index = 0; index < 17; index++) {
    const relative = 'front_end/large-' + index + '.bin';
    sparse(path.join(f.frontendRoot, relative), MiB);
    f.entries.push(f.fileEntry(relative));
  }
  f.saveManifest();
  await rejectsPrepare(f);
});

test('frontend inventory is limited to 1024 members', async t => {
  const f = fixture(t);
  for (let index = 0; index < 1023; index++) {
    const relative = 'front_end/small/' + index + '.txt';
    f.source(relative, 'x');
    f.entries.push(f.fileEntry(relative));
  }
  f.saveManifest();
  await rejectsPrepare(f);
});

for (const member of ['approved-manifest.json', 'package-approval.json']) {
  test(member + ' input is limited to 512 KiB', async t => {
    const f = fixture(t);
    const value = member === 'approved-manifest.json' ? f.entries : f.approval;
    const bytes = json(value) + ' '.repeat(512 * 1024);
    f.source(member, bytes);
    if (member === 'approved-manifest.json') {
      f.manifestSha256 = sha256(bytes);
      f.approval.frontend.sha256 = f.manifestSha256;
      f.saveApproval();
    } else f.approvalSha256 = sha256(bytes);
    await rejectsPrepare(f);
  });
}

test('kernel input is limited to 256 MiB before any full allocation', async t => {
  const f = fixture(t);
  sparse(path.join(f.frontendRoot, f.kernelRelative), 256 * MiB + 1);
  await rejectsPrepare(f);
});

test('kernel verification streams bytes during real preparation and deployment', async t => {
  const f = fixture(t);
  const bytes = Buffer.concat([syntheticPe(f.architecture), Buffer.alloc(2 * MiB)]);
  f.source(f.kernelRelative, bytes);
  f.kernelSha256 = sha256(bytes);
  f.approval.kernel.sha256 = f.kernelSha256;
  f.saveApproval();
  f.installKernel();
  const originalRead = fs.readFileSync;
  fs.readFileSync = function (file, ...rest) {
    if (typeof file === 'string' && path.basename(file).toLowerCase() === 'node.dll')
      throw new Error('Synthetic guard: kernel verification must not read a whole DLL into memory');
    return originalRead.call(this, file, ...rest);
  };
  try {
    await f.prepare();
    await f.deploy();
  } finally { fs.readFileSync = originalRead; }
  assert.equal(sha256(fs.readFileSync(path.join(f.applicationOutput, 'node.dll'))), f.kernelSha256);
});

for (const field of ['preparedRoot', 'applicationOutput', 'kernelSha256', 'manifestSha256',
  'approvalSha256', 'architecture', 'sdkVersion']) {
  test('deploy requires an explicit ' + field, async t => {
    const f = fixture(t);
    await f.prepare();
    f.installKernel();
    const before = inventory(f.applicationOutput);
    await rejectsDevtools(() => f.deploy({[field]: undefined}));
    assert.deepEqual(inventory(f.applicationOutput), before);
  });
}

for (const field of ['kernelSha256', 'manifestSha256', 'approvalSha256']) {
  test('deploy rejects an external ' + field + ' mismatch without touching the app', async t => {
    const f = fixture(t);
    await f.prepare();
    f.installKernel();
    f.application('userdata/keep.txt', 'Keep these application bytes.\n');
    const before = inventory(f.applicationOutput);
    await rejectsDevtools(() => f.deploy({[field]: '0'.repeat(64)}));
    assert.deepEqual(inventory(f.applicationOutput), before);
  });
}

for (const relative of ['frontend/front_end/inspector.html', 'frontend/approved-manifest.json',
  'kernel/node.dll', 'legal/licenses/LICENSE.txt', 'approval.json', 'package.json']) {
  test('deploy rejects staged tampering of ' + relative, async t => {
    const f = fixture(t);
    await f.prepare();
    f.installKernel();
    f.application('devtools/package/previous.txt', 'Previously deployed package.\n');
    f.application('userdata/keep.txt', 'Keep these application bytes.\n');
    const before = inventory(f.applicationOutput);
    fs.appendFileSync(path.join(f.packageRoot, relative), '\nchanged\n');
    await rejectsDevtools(() => f.deploy());
    assert.deepEqual(inventory(f.applicationOutput), before);
  });
}

test('deploy rejects unrecorded staged frontend files', async t => {
  const f = fixture(t);
  await f.prepare();
  f.installKernel();
  f.write(path.join(f.packageRoot, 'frontend', 'front_end', 'extra.js'), 'window.extra = true;');
  const before = inventory(f.applicationOutput);
  await rejectsDevtools(() => f.deploy());
  assert.deepEqual(inventory(f.applicationOutput), before);
});

test('deploy rejects a staged hard link rather than trusting equal bytes', async t => {
  const f = fixture(t);
  await f.prepare();
  f.installKernel();
  fs.linkSync(path.join(f.packageRoot, 'frontend', 'front_end', 'inspector.js'),
    path.join(f.base, 'staged-hard-link.js'));
  const before = inventory(f.applicationOutput);
  await rejectsDevtools(() => f.deploy());
  assert.deepEqual(inventory(f.applicationOutput), before);
});

test('deploy rejects a missing application node.dll before creating devtools', async t => {
  const f = fixture(t);
  await f.prepare();
  f.application('userdata/keep.txt', 'Keep these application bytes.\n');
  const before = inventory(f.applicationOutput);
  await rejectsDevtools(() => f.deploy());
  assert.deepEqual(inventory(f.applicationOutput), before);
  assert.equal(fs.existsSync(path.join(f.applicationOutput, 'devtools')), false);
});

test('deploy binds the actual application node.dll and preserves an old package on mismatch', async t => {
  const f = fixture(t);
  await f.prepare();
  f.installKernel();
  fs.appendFileSync(path.join(f.applicationOutput, 'node.dll'), 'tampered');
  f.application('devtools/package/previous.txt', 'Previously deployed package.\n');
  f.application('userdata/keep.txt', 'Keep these application bytes.\n');
  const before = inventory(f.applicationOutput);
  await rejectsDevtools(() => f.deploy());
  assert.deepEqual(inventory(f.applicationOutput), before);
});

test('deployment publication failure rolls back the old package and preserves other app files', async t => {
  const f = fixture(t);
  await f.prepare();
  f.installKernel();
  await f.deploy();
  f.application('devtools/settings.json', '{"applicationOwned":true}\n');
  f.application('userdata/keep.txt', 'Keep these application bytes.\n');
  const before = inventory(f.applicationOutput);
  f.source('front_end/inspector.js', 'window.syntheticInspector = "new package";\n');
  f.entries = f.entries.map(entry => f.fileEntry(entry.path));
  f.saveManifest();
  await f.prepare();
  const originalRename = fs.renameSync;
  let injected = false;
  fs.renameSync = function (source, destination, ...rest) {
    if (!injected && path.resolve(String(destination)) === path.resolve(f.deployedRoot)) {
      injected = true;
      const error = new Error('Synthetic package publication failure');
      error.code = 'EPERM';
      throw error;
    }
    return originalRename.call(this, source, destination, ...rest);
  };
  try { await rejectsDevtools(() => f.deploy()); }
  finally { fs.renameSync = originalRename; }
  assert.equal(injected, true, 'The test must exercise the actual publication rename');
  assert.deepEqual(inventory(f.applicationOutput), before,
    'The prior package, actual kernel and application-owned data must survive rollback');
});

async function deliveryFixture(t) {
  const f = fixture(t);
  await f.prepare();
  f.installKernel();
  await f.deploy();
  f.metadataPath = path.join(f.deployedRoot, 'package.json');
  f.metadata = JSON.parse(fs.readFileSync(f.metadataPath, 'utf8'));
  return f;
}

async function rejectsDelivery(f) {
  const before = inventory(f.applicationOutput);
  await rejectsDevtools(() => devtools.verifyDelivery(f.applicationOutput));
  assert.deepEqual(inventory(f.applicationOutput), before, 'Delivery verification must be read-only');
}

test('verifyDelivery returns verified metadata and exact app-relative package paths', async t => {
  const f = await deliveryFixture(t);
  f.application('userdata/keep.txt', 'Application state is outside the package.\n');
  f.application('devtools/settings.json', '{"applicationOwned":true}\n');
  const result = await devtools.verifyDelivery(f.applicationOutput);
  assert.deepEqual(result.metadata, f.metadata);
  assert.deepEqual([...result.paths].sort(), [
    'devtools/package/frontend/approved-manifest.json',
    'devtools/package/frontend/front_end/inspector.html',
    'devtools/package/frontend/front_end/inspector.js',
    'devtools/package/legal/licenses/LICENSE.txt',
    'devtools/package/package.json',
  ]);
  assert.deepEqual(result.paths, result.files.map(entry => entry.path));
  assert.deepEqual(result.files, result.paths.map(relative => {
    const bytes = fs.readFileSync(path.join(f.applicationOutput, relative));
    return {path: relative, bytes: bytes.length, sha256: sha256(bytes)};
  }));
});

test('verifyDelivery rejects a missing package without creating it', async t => {
  const f = fixture(t);
  f.installKernel();
  f.application('userdata/keep.txt', 'Keep application state.\n');
  await rejectsDelivery(f);
  assert.equal(fs.existsSync(path.join(f.applicationOutput, 'devtools')), false);
});

test('verifyDelivery rejects invalid delivery identity fields', async t => {
  const f = await deliveryFixture(t);
  for (const change of [
    metadata => { metadata.schema = 'unknown/v1'; },
    metadata => { metadata.mode = 'prepared'; },
    metadata => { metadata.backend = 'webview2'; },
    metadata => { metadata.architecture = 'x86'; },
    metadata => { metadata.sdk_version = ''; },
    metadata => { metadata.kernel.version = ''; },
    metadata => { metadata.frontend.version = ''; },
    metadata => { metadata.kernel.sha256 = 'invalid'; },
    metadata => { metadata.frontend.manifest_sha256 = 'invalid'; },
    metadata => { metadata.approval_sha256 = 'invalid'; },
  ]) {
    const metadata = structuredClone(f.metadata);
    change(metadata);
    fs.writeFileSync(f.metadataPath, json(metadata));
    await rejectsDelivery(f);
  }
});

test('verifyDelivery binds the manifest hash and its independent frontend roster', async t => {
  const f = await deliveryFixture(t);
  const manifestPath = path.join(f.deployedRoot, 'frontend', 'approved-manifest.json');
  const changed = json(f.entries.filter(entry => entry.path !== 'front_end/inspector.js'));
  fs.writeFileSync(manifestPath, changed);
  await rejectsDelivery(f);
  // Rebinding metadata to these manifest bytes cannot authorize its omitted frontend member.
  const metadata = structuredClone(f.metadata);
  metadata.frontend.manifest_sha256 = sha256(changed);
  const row = metadata.files.find(entry => entry.path === 'frontend/approved-manifest.json');
  row.bytes = Buffer.byteLength(changed);
  row.sha256 = sha256(changed);
  fs.writeFileSync(f.metadataPath, json(metadata));
  await rejectsDelivery(f);
});

test('verifyDelivery rejects extra actual frontend and legal members', async t => {
  const f = await deliveryFixture(t);
  for (const relative of ['frontend/front_end/extra.js', 'legal/licenses/extra.txt']) {
    const file = f.write(path.join(f.deployedRoot, relative), 'Unrecorded bytes.\n');
    await rejectsDelivery(f);
    fs.unlinkSync(file);
  }
});

test('verifyDelivery hashes actual frontend and legal members and bounds legal resources', async t => {
  const f = await deliveryFixture(t);
  for (const relative of ['frontend/front_end/inspector.js', 'legal/licenses/LICENSE.txt']) {
    const file = path.join(f.deployedRoot, relative), original = fs.readFileSync(file);
    fs.appendFileSync(file, 'changed');
    await rejectsDelivery(f);
    fs.writeFileSync(file, original);
  }
  const bytes = Buffer.alloc(MiB + 1), metadata = structuredClone(f.metadata);
  const row = metadata.files.find(entry => entry.path === 'legal/licenses/LICENSE.txt');
  row.bytes = bytes.length;
  row.sha256 = sha256(bytes);
  fs.writeFileSync(path.join(f.deployedRoot, row.path), bytes);
  fs.writeFileSync(f.metadataPath, json(metadata));
  await rejectsDelivery(f);
});

test('verifyDelivery rejects hard-linked package members with unchanged bytes', async t => {
  const f = await deliveryFixture(t);
  for (const relative of ['frontend/front_end/inspector.js', 'legal/licenses/LICENSE.txt']) {
    const alias = path.join(f.base, 'alias');
    fs.linkSync(path.join(f.deployedRoot, relative), alias);
    await rejectsDelivery(f);
    fs.unlinkSync(alias);
  }
});

test('verifyDelivery binds the actual application kernel bytes and PE architecture', async t => {
  const f = await deliveryFixture(t);
  const kernel = path.join(f.applicationOutput, 'node.dll');
  fs.appendFileSync(kernel, 'changed');
  await rejectsDelivery(f);
  const bytes = syntheticPe('x86'), metadata = structuredClone(f.metadata);
  fs.writeFileSync(kernel, bytes);
  metadata.kernel.sha256 = sha256(bytes);
  fs.writeFileSync(f.metadataPath, json(metadata));
  await rejectsDelivery(f);
});
