'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const runtime = require('../../tools/internal/runtime-deployment.cjs');
const id = require('../../tools/internal/delivery-identity.cjs');
const sdkRoot = path.resolve(__dirname, '../..');
const manifestFile = 'tools/internal/runtime-files.json';

function image(architecture, salt = 0) {
  const data = Buffer.alloc(128, salt);
  data.write('MZ'); data.writeUInt32LE(64, 0x3c); data.write('PE\0\0', 64);
  data.writeUInt16LE(architecture === 'x64' ? 0x8664 : 0x14c, 68);
  return data;
}
function fixture(t) {
  const owner = fs.realpathSync(os.tmpdir());
  const root = fs.realpathSync(fs.mkdtempSync(path.join(owner, 'soui-runtime-')));
  t.after(() => {
    assert.equal(path.dirname(root), owner); assert.ok(path.basename(root).startsWith('soui-runtime-'));
    fs.rmSync(id.safePath(root), {recursive: true, force: true});
  });
  const put = (name, data) => {
    const file = id.safePath(root, name); fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, data); return file;
  };
  const manifest = id.readJson(path.join(sdkRoot, manifestFile));
  for (const architecture of ['x86', 'x64']) {
    manifest.architectures[architecture] = ['crt', 'soui-core', 'soui-gdi', 'soui-resources', 'miniblink'].map((component, index) => {
      const name = component === 'miniblink' ? 'node.dll' : `${component}.dll`;
      const data = image(architecture, index + 1), source = `third_party/prebuilt/${architecture}/fixture/${name}`;
      put(source, data);
      return {source, path: name, bytes: data.length, sha256: id.hash(data), component};
    });
  }
  const save = () => put(manifestFile, id.encoded(manifest));
  save();
  return {root, put, manifest, save, app: path.join(root, 'application'), project: path.join(root, 'project')};
}
const snapshot = root => id.encoded(id.walk(root).map(name => ({name, sha256: id.hash(fs.readFileSync(id.safePath(root, name)))})));

test('real x86/x64 manifests preserve the previous complete flat runtime sets and modern CRT', () => {
  for (const arch of ['x86', 'x64']) {
    const base = `third_party/prebuilt/${arch}`;
    const soui = fs.readdirSync(path.join(sdkRoot, `${base}/soui/bin`)).filter(name => /\.dll$/i.test(name) &&
      !/^(souid|utilitiesd|render-gdid|imgdecoder-gdipd|soui-sys-resourced)\.dll$/i.test(name)).map(name => `${base}/soui/bin/${name}`);
    const crt = id.walk(sdkRoot, `${base}/runtime`).filter(name => /\.dll$/i.test(name));
    const expected = [...soui, ...crt, `${base}/miniblink/node.dll`].sort();
    const plan = runtime.resolve(sdkRoot, arch);
    assert.deepEqual(plan.files.map(file => file.source).sort(), expected);
    assert.deepEqual(runtime.resolve(sdkRoot, arch, 'webview2').files.map(file => file.source).sort(), crt.sort());
    assert.equal(plan.files.length, 59);
  }
});

test('default, project-selected components and dependencies resolve deterministically', t => {
  const f = fixture(t);
  const full = runtime.resolve(f.root, 'x64');
  assert.equal(full.files.length, 5);
  f.put('project/project.json', JSON.stringify({runtimeComponents: ['miniblink']}));
  const small = runtime.resolve(f.root, 'x64', 'miniblink', {projectRoot: f.project});
  assert.deepEqual(small.components, ['crt', 'miniblink', 'soui-core']);
  assert.equal(small.files.length, 3);
  assert.equal(id.encoded(small), id.encoded(runtime.resolve(f.root, 'x64', 'miniblink', {components: ['miniblink']})));
  assert.equal(runtime.resolve(f.root, 'x64', 'webview2').files.length, 1);
  assert.equal(id.encoded(full).includes(f.root), false);
});

for (const value of [null, {}, 'crt', [], ['bogus'], ['crt', 'crt'], [1], ['../crt'], ['soui-core', 'miniblink', 'crt', 'soui-gdi', null]]) {
  test(`reject invalid runtimeComponents ${JSON.stringify(value)} before destination writes`, t => {
    const f = fixture(t); f.put('project/project.json', JSON.stringify({runtimeComponents: value}));
    assert.throws(() => runtime.deploy(f.root, f.app, 'x64', 'miniblink', {projectRoot: f.project}), /E_RUNTIME/);
    assert.equal(fs.existsSync(f.app), false);
  });
}

for (const [label, mutate] of [
  ['unsupported schema', m => { m.schema = 'bad'; }],
  ['duplicate destination', m => { m.architectures.x64[1].path = m.architectures.x64[0].path; }],
  ['case collision', m => { m.architectures.x64[1].path = m.architectures.x64[0].path.toUpperCase(); }],
  ['foreign architecture source', m => { m.architectures.x64[0].source = m.architectures.x86[0].source; }],
  ['traversal source', m => { m.architectures.x64[0].source = '../file.dll'; }],
  ['reserved destination', m => { m.architectures.x64[0].path = 'con.dll'; }],
  ['alternate stream', m => { m.architectures.x64[0].path = 'file:evil.dll'; }],
  ['nested destination', m => { m.architectures.x64[0].path = 'dir/file.dll'; }],
  ['dependency cycle', m => { m.components.crt.dependsOn = ['soui-core']; }],
  ['unknown dependency', m => { m.components.crt.dependsOn = ['unknown']; }],
  ['unknown backend components', m => { m.backends.webview2.required = ['unknown']; }],
  ['unsafe integer size', m => { m.architectures.x64[0].bytes = Number.MAX_SAFE_INTEGER + 1; }],
  ['unknown field in unselected architecture', m => { m.architectures.x86[0].unknown = true; }],
]) test(`manifest rejects ${label}`, t => {
  const f = fixture(t); mutate(f.manifest); f.save();
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /E_RUNTIME|Invalid path/);
  assert.equal(fs.existsSync(f.app), false);
});

test('source identity, missing image and PE architecture are verified before writing', t => {
  const f = fixture(t), file = f.manifest.architectures.x64[0];
  f.put(file.source, image('x86', 1));
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /identity mismatch/);
  file.sha256 = id.hash(image('x86', 1)); f.save();
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /architecture mismatch/);
  fs.unlinkSync(path.join(f.root, file.source));
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /ENOENT/);
  assert.equal(fs.existsSync(f.app), false);
});

test('public option validation rejects unknown fields and unsafe scalar types', t => {
  const f = fixture(t);
  for (const options of [null, [], {component: ['crt']}, {adoptLegacy: 1}, {projectRoot: 1}, {kernelSource: {}}, {kernelSha256: 1}]) {
    assert.throws(() => runtime.deploy(f.root, f.app, 'x64', 'miniblink', options), /E_RUNTIME/);
  }
  for (const [architecture, backend] of [['arm64', 'miniblink'], ['x64', 'other']]) {
    assert.throws(() => runtime.resolve(f.root, architecture, backend), /unsupported architecture\/backend/);
  }
  assert.throws(() => runtime.resolve(f.root, 'x64', 'webview2', {components: ['miniblink']}), /runtimeComponents/);
  assert.equal(fs.existsSync(f.app), false);
});

test('size preflight refuses oversized or mismatched runtime files before reading their data', t => {
  const f = fixture(t), file = f.manifest.architectures.x64[0];
  const target = path.join(f.root, file.source), originalStat = fs.statSync, originalRead = fs.readFileSync;
  let readRuntime = false;
  fs.statSync = function(name, options) {
    const stat = originalStat(name, options);
    if (path.resolve(name) === target) stat.size = 512 * 1024 * 1024 + 1;
    return stat;
  };
  fs.readFileSync = function(name, options) {
    if (path.resolve(name) === target) readRuntime = true;
    return originalRead(name, options);
  };
  try { assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /identity mismatch/); }
  finally { fs.statSync = originalStat; fs.readFileSync = originalRead; }
  assert.equal(readRuntime, false);
  assert.equal(fs.existsSync(f.app), false);
});

test('hard linked sources, destinations and receipts are rejected without modifying their aliases', t => {
  const f = fixture(t), file = f.manifest.architectures.x64[0];
  const source = path.join(f.root, file.source), alias = path.join(f.root, 'alias.dll');
  fs.linkSync(source, alias);
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /unlinked file/);
  fs.unlinkSync(alias);
  runtime.deploy(f.root, f.app, 'x64');
  fs.linkSync(path.join(f.app, file.path), alias);
  const before = snapshot(f.app);
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /bounded file/);
  assert.equal(snapshot(f.app), before);
  fs.unlinkSync(alias);
  fs.linkSync(path.join(f.app, runtime.receiptName), path.join(f.root, 'receipt-alias.json'));
  assert.throws(() => runtime.verifyDirectory(f.app), /receipt file/);
});

test('deployment creates an auditable receipt and retires only previously owned component files', t => {
  const f = fixture(t); f.put('application/business.dll', 'business');
  const first = runtime.deploy(f.root, f.app, 'x64');
  assert.deepEqual(runtime.verifyDirectory(f.app), JSON.parse(id.encoded(first)));
  const second = runtime.deploy(f.root, f.app, 'x64', 'miniblink', {components: ['miniblink']});
  assert.equal(fs.existsSync(path.join(f.app, 'soui-gdi.dll')), false);
  assert.equal(fs.existsSync(path.join(f.app, 'soui-resources.dll')), false);
  assert.equal(fs.readFileSync(path.join(f.app, 'business.dll'), 'utf8'), 'business');
  assert.equal(second.files.length, 3);
  assert.deepEqual(runtime.verifyDirectory(f.app, second), JSON.parse(id.encoded(second)));
  assert.equal(fs.readdirSync(f.app).some(name => /^\.runtime-/.test(name)), false);
});

test('unchanged complete deployment verifies all files and performs no directory creation or rename', t => {
  const f = fixture(t), first = runtime.deploy(f.root, f.app, 'x64');
  const originalMkdir = fs.mkdirSync, originalRename = fs.renameSync;
  fs.mkdirSync = () => { throw new Error('unexpected directory creation'); };
  fs.renameSync = () => { throw new Error('unexpected rename'); };
  try {
    assert.equal(id.encoded(runtime.deploy(f.root, f.app, 'x64')), id.encoded(first));
    fs.writeFileSync(path.join(f.app, 'crt.dll'), 'changed under unchanged receipt');
    assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /unowned or modified runtime conflict/);
  } finally { fs.mkdirSync = originalMkdir; fs.renameSync = originalRename; }
});

test('unchanged receipt with a missing owned file repairs the deployment', t => {
  const f = fixture(t), first = runtime.deploy(f.root, f.app, 'x64');
  fs.unlinkSync(path.join(f.app, 'crt.dll'));
  assert.equal(id.encoded(runtime.deploy(f.root, f.app, 'x64')), id.encoded(first));
  assert.equal(runtime.verifyDirectory(f.app, first).files.length, 5);
});

test('legacy equal files are adopted; incompatible files require explicit first-deployment migration', t => {
  const f = fixture(t), file = f.manifest.architectures.x64[0];
  f.put(`application/${file.path}`, fs.readFileSync(path.join(f.root, file.source)));
  runtime.deploy(f.root, f.app, 'x64');
  fs.unlinkSync(path.join(f.app, runtime.receiptName));
  f.put(`application/${file.path}`, image('x64', 99));
  f.put('application/business.dll', 'keep');
  const before = snapshot(f.app);
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /unowned or modified/);
  assert.equal(snapshot(f.app), before);
  runtime.deploy(f.root, f.app, 'x64', 'miniblink', {adoptLegacy: true});
  assert.equal(runtime.verifyDirectory(f.app).files.length, 5);
  assert.equal(fs.readFileSync(path.join(f.app, 'business.dll'), 'utf8'), 'keep');
});

test('existing owned files support SDK upgrades, but modified files block overwrite or retirement', t => {
  const f = fixture(t); runtime.deploy(f.root, f.app, 'x64');
  const file = f.manifest.architectures.x64[0], data = image('x64', 92);
  f.put(file.source, data); file.sha256 = id.hash(data); f.save();
  runtime.deploy(f.root, f.app, 'x64');
  assert.deepEqual(fs.readFileSync(path.join(f.app, file.path)), data);
  f.put('application/soui-gdi.dll', 'modified'); const before = snapshot(f.app);
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64', 'miniblink', {components: ['miniblink'], adoptLegacy: true}), /modified/);
  assert.equal(snapshot(f.app), before);
  assert.throws(() => runtime.verifyDirectory(f.app), /identity mismatch/);
});

test('receipt validation refuses tampering and expected plans bind metadata', t => {
  const f = fixture(t), plan = runtime.deploy(f.root, f.app, 'x64');
  const receipt = JSON.parse(id.encoded(plan)); receipt.manifestSha256 = '0'.repeat(64);
  f.put(`application/${runtime.receiptName}`, id.encoded(receipt));
  assert.throws(() => runtime.verifyDirectory(f.app, plan), /does not match/);
  receipt.files[0].path = '../escape.dll'; f.put(`application/${runtime.receiptName}`, id.encoded(receipt));
  assert.throws(() => runtime.verifyDirectory(f.app), /E_RUNTIME|Invalid path/);
  fs.unlinkSync(path.join(f.app, runtime.receiptName));
  assert.throws(() => runtime.verifyDirectory(f.app), /receipt missing/);
});

test('explicit paired kernel keeps its external approval hash and ordinary kernel does not overwrite it', t => {
  const f = fixture(t), data = image('x64', 77);
  const kernelSource = f.put('approved/node.dll', data), kernelSha256 = id.hash(data);
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64', 'miniblink', {kernelSource}), /requires explicit SHA-256/);
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64', 'miniblink', {kernelSource, kernelSha256: '0'.repeat(64)}), /identity mismatch/);
  const plan = runtime.deploy(f.root, f.app, 'x64', 'miniblink', {kernelSource, kernelSha256});
  assert.deepEqual(plan.kernel, {external: true, sha256: kernelSha256});
  assert.equal(plan.files.find(file => file.path === 'node.dll').sha256, kernelSha256);
  assert.equal(id.encoded(plan).includes(kernelSource), false);
  runtime.verifyDirectory(f.app, plan);
  runtime.deploy(f.root, f.app, 'x64', 'miniblink', {kernelSource, kernelSha256});
  assert.deepEqual(fs.readFileSync(path.join(f.app, 'node.dll')), data);
  f.put('application/devtools/frontend.js', 'paired frontend');
  const before = snapshot(f.app);
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /paired kernel transition requires explicit clean deployment\/approval/);
  assert.equal(snapshot(f.app), before);
  const next = image('x64', 78), nextSource = f.put('approved-next/node.dll', next);
  runtime.deploy(f.root, f.app, 'x64', 'miniblink', {kernelSource: nextSource, kernelSha256: id.hash(next)});
  assert.deepEqual(fs.readFileSync(path.join(f.app, 'node.dll')), next);
});

for (const migrate of [false, true]) test(`publication failure restores ${migrate ? 'legacy' : 'receipt-owned'} deployment completely`, t => {
  const f = fixture(t);
  if (migrate) f.put('application/crt.dll', image('x64', 99));
  else runtime.deploy(f.root, f.app, 'x64');
  f.put('application/business.dll', 'keep');
  const before = snapshot(f.app), original = fs.renameSync;
  let publications = 0;
  fs.renameSync = function(source, destination) {
    if (path.basename(path.dirname(source)).startsWith('.runtime-new-') && ++publications === 2) throw new Error('injected publication failure');
    return original(source, destination);
  };
  try {
    assert.throws(() => runtime.deploy(f.root, f.app, 'x64', 'miniblink', {adoptLegacy: migrate, components: ['miniblink']}), /injected publication failure/);
  } finally { fs.renameSync = original; }
  assert.equal(snapshot(f.app), before);
});

test('copy failure and concurrent destination mutation do not publish a partial selection', t => {
  const f = fixture(t); runtime.deploy(f.root, f.app, 'x64');
  const before = snapshot(f.app), original = fs.copyFileSync;
  fs.copyFileSync = () => { throw new Error('injected copy failure'); };
  try { assert.throws(() => runtime.deploy(f.root, f.app, 'x64', 'miniblink', {components: ['miniblink']}), /injected copy failure/); }
  finally { fs.copyFileSync = original; }
  assert.equal(snapshot(f.app), before);
  let changed = false;
  fs.copyFileSync = function(source, destination, flags) {
    original(source, destination, flags);
    if (!changed) { changed = true; f.put('application/crt.dll', 'concurrent writer'); }
  };
  try { assert.throws(() => runtime.deploy(f.root, f.app, 'x64', 'miniblink', {components: ['miniblink']}), /destination changed/); }
  finally { fs.copyFileSync = original; }
  assert.equal(fs.readFileSync(path.join(f.app, 'crt.dll'), 'utf8'), 'concurrent writer');
});

test('failure creating the backup directory removes only the owned temporary stage', t => {
  const f = fixture(t); runtime.deploy(f.root, f.app, 'x64');
  f.put('application/business/data.txt', 'keep');
  const before = snapshot(f.app), original = fs.mkdirSync;
  fs.mkdirSync = function(name, options) {
    if (path.basename(name).startsWith('.runtime-old-')) throw new Error('injected backup directory failure');
    return original(name, options);
  };
  try { assert.throws(() => runtime.deploy(f.root, f.app, 'x64', 'miniblink', {components: ['miniblink']}), /injected backup directory failure/); }
  finally { fs.mkdirSync = original; }
  assert.equal(snapshot(f.app), before);
  assert.equal(fs.readdirSync(f.app).some(name => /^\.runtime-/.test(name)), false);
});

test('linked sources, destination roots and receipts are rejected when links are permitted', t => {
  const f = fixture(t), source = f.manifest.architectures.x64[0].source;
  const original = f.put('elsewhere.dll', image('x64', 1));
  fs.unlinkSync(path.join(f.root, source));
  try { fs.symlinkSync(original, path.join(f.root, source), 'file'); }
  catch (error) { if (['EPERM', 'EACCES'].includes(error.code)) { t.skip('File symbolic-link permission unavailable'); return; } throw error; }
  assert.throws(() => runtime.deploy(f.root, f.app, 'x64'), /Linked path/);
  fs.unlinkSync(path.join(f.root, source)); f.put(source, image('x64', 1));
  runtime.deploy(f.root, f.app, 'x64');
  const receipt = path.join(f.app, runtime.receiptName), saved = f.put('receipt.json', fs.readFileSync(receipt));
  fs.unlinkSync(receipt); fs.symlinkSync(saved, receipt, 'file');
  assert.throws(() => runtime.verifyDirectory(f.app), /Linked path/);
});

test('CLI rejects malformed options and supports first-build legacy migration', t => {
  const f = fixture(t), script = path.join(sdkRoot, 'tools/internal/runtime-deployment.cjs');
  f.put('application/crt.dll', image('x64', 99));
  const args = ['deploy', '--sdk-root', f.root, '--application', f.app, '--architecture', 'x64', '--backend', 'miniblink'];
  assert.notEqual(spawnSync(process.execPath, [script, ...args]).status, 0);
  assert.equal(spawnSync(process.execPath, [script, ...args, '--adopt-legacy']).status, 0);
  runtime.verifyDirectory(f.app);
  for (const more of [['--unknown', 'value'], ['--architecture', 'x86'], ['--project-root']]) {
    assert.notEqual(spawnSync(process.execPath, [script, ...args, ...more]).status, 0);
  }
});
