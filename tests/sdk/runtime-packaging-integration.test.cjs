'use strict';

// The native runtimes below are the SDK's unchanged, architecture-pinned DLLs.
// EXE fixtures and packaging stubs are inert; these tests never execute them.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const id = require('../../tools/internal/delivery-identity.cjs');
const runtime = require('../../tools/internal/runtime-deployment.cjs');
const bundle = require('../../tools/internal/runtime-bundle.cjs');
const sdk = path.resolve(__dirname, '../..');
const prefix = 'soui-runtime-packaging-integration-';

function fixture(t) {
  const temporary = fs.realpathSync(os.tmpdir());
  const root = fs.realpathSync(fs.mkdtempSync(path.join(temporary, prefix)));
  t.after(() => {
    assert.equal(path.dirname(root), temporary);
    assert.ok(path.basename(root).startsWith(prefix));
    assert.equal(id.safePath(root), root);
    assert.equal(fs.realpathSync(root), root);
    fs.rmSync(root, {recursive: true, force: true});
  });
  return root;
}
function put(root, relative, bytes) {
  const file = id.safePath(root, relative);
  fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, bytes);
}
function app(root, target, architecture, withReceipt = true) {
  fs.mkdirSync(root, {recursive: true});
  put(root, target + '.exe', 'SYNTHETIC EXE: NEVER RUN\0');
  put(root, 'resources/index.html', '<!doctype html><main>Same shared fixture</main>');
  if (withReceipt) runtime.deploy(sdk, root, architecture);
  else put(root, 'node.dll', 'SYNTHETIC LEGACY DLL: NEVER LOADED\0');
  const report = {schema: 'soui.application-size/v1', version: '0.5.0', target, architecture,
    configuration: 'Release', rendererBackend: 'miniblink', buildId: 'inert-fixture-' + target,
    sourceSha256: id.hash('synthetic fixture source'), releaseReady: false,
    payload: id.payload(root, target)};
  return {name: target, root, report};
}
function oldDefaultSources(architecture) {
  const base = `third_party/prebuilt/${architecture}`;
  const soui = fs.readdirSync(path.join(sdk, base, 'soui/bin')).filter(name => /\.dll$/i.test(name) &&
    !/(souid|utilitiesd|render-gdid|imgdecoder-gdipd|soui-sys-resourced)\.dll$/i.test(name))
    .map(name => `${base}/soui/bin/${name}`);
  const crt = id.walk(sdk, `${base}/runtime`).filter(name => /\.dll$/i.test(name));
  return [...soui, ...crt, `${base}/miniblink/node.dll`].sort();
}

for (const architecture of ['x86', 'x64']) {
  test(`${architecture} manifest default deploys exactly the former DLL set and binds payload identity`, t => {
    const root = fixture(t);
    const plan = runtime.resolve(sdk, architecture);
    assert.deepEqual(plan.files.map(file => file.source).sort(), oldDefaultSources(architecture));
    const application = app(path.join(root, 'application'), 'DefaultApp', architecture);
    const receipt = runtime.verifyDirectory(application.root, plan);
    assert.equal(receipt.architecture, architecture);
    assert.equal(receipt.backend, 'miniblink');
    assert.equal(application.report.payload.files.filter(file => /\.dll$/i.test(file.path)).length, plan.files.length);
    assert.ok(application.report.payload.files.some(file => file.path === runtime.receiptName));
    for (const file of receipt.files) {
      const data = fs.readFileSync(path.join(application.root, file.path));
      assert.equal(data.length, file.bytes);
      assert.equal(id.hash(data), file.sha256);
    }
    const modern = runtime.resolve(sdk, architecture, 'webview2');
    assert.deepEqual(modern.files.map(file => file.source).sort(),
      id.walk(sdk, `third_party/prebuilt/${architecture}/runtime`).filter(name => /\.dll$/i.test(name)).sort());
    assert.equal(modern.files.some(file => file.path === 'node.dll'), false);
  });
}

test('legacy application without a runtime receipt still captures and verifies its existing payload', t => {
  const root = fixture(t), application = app(path.join(root, 'legacy'), 'LegacyApp', 'x64', false);
  assert.equal(fs.existsSync(path.join(application.root, runtime.receiptName)), false);
  assert.equal(id.verifyPayload(application.root, 'LegacyApp', application.report.payload).sha256,
    application.report.payload.sha256);
  const output = path.join(root, 'bundle');
  bundle.pack(output, [application]);
  bundle.restore(output, path.join(root, 'restored'));
  assert.equal(id.verifyPayload(path.join(root, 'restored/LegacyApp'), 'LegacyApp', application.report.payload).sha256,
    application.report.payload.sha256);
});

test('shared runtime blobs deduplicate by content, retain distinct architectures, and restore independent applications', t => {
  const root = fixture(t);
  const applications = [app(path.join(root, 'first'), 'FirstApp', 'x64'),
    app(path.join(root, 'second'), 'SecondApp', 'x64'), app(path.join(root, 'third'), 'ThirdApp', 'x86')];
  const kernel = application => application.report.payload.files.find(file => file.path === 'node.dll');
  assert.equal(kernel(applications[0]).sha256, kernel(applications[1]).sha256);
  assert.notEqual(kernel(applications[0]).sha256, kernel(applications[2]).sha256);
  const output = path.join(root, 'bundle'), packed = bundle.pack(output, applications);
  const manifest = id.readJson(path.join(output, 'manifest.json'));
  const expected = new Map(applications.flatMap(application => application.report.payload.files)
    .map(file => [file.sha256, file.bytes]));
  assert.equal(manifest.blobs.length, expected.size);
  assert.equal(fs.readdirSync(path.join(output, 'blobs')).length, expected.size);
  assert.equal(manifest.blobs.filter(blob => blob.sha256 === kernel(applications[0]).sha256).length, 1);
  assert.equal(manifest.blobs.filter(blob => blob.sha256 === kernel(applications[2]).sha256).length, 1);
  assert.ok(packed.savedUncompressedBytes >= runtime.resolve(sdk, 'x64').files.reduce((n, file) => n + file.bytes, 0));
  for (const application of applications) fs.renameSync(application.root, application.root + '-withdrawn');
  assert.equal(bundle.verify(output).verified, true, 'The package must not depend on original source directories');
  const destination = path.join(root, 'restored');
  assert.equal(bundle.restore(output, destination).restored, true);
  for (const application of applications) {
    const restored = path.join(destination, application.name);
    assert.equal(id.verifyPayload(restored, application.report.target, application.report.payload).sha256,
      application.report.payload.sha256);
    assert.equal(runtime.verifyDirectory(restored).architecture, application.report.architecture);
    for (const file of application.report.payload.files) {
      const full = path.join(restored, file.path);
      assert.equal(fs.lstatSync(full).nlink, 1, 'Restored files must remain independent ordinary copies');
      assert.equal(id.hash(fs.readFileSync(full)), file.sha256);
    }
  }
  const secondKernel = path.join(destination, 'SecondApp/node.dll');
  fs.writeFileSync(path.join(destination, 'FirstApp/node.dll'), 'MODIFIED RESTORED INSTANCE');
  assert.equal(id.hash(fs.readFileSync(secondKernel)), kernel(applications[1]).sha256);
  assert.throws(() => runtime.verifyDirectory(path.join(destination, 'FirstApp')), /identity|PE|runtime/i);
});

test('a corrupt runtime receipt is rejected before it becomes new application evidence', t => {
  const root = fixture(t), application = app(path.join(root, 'app'), 'ReceiptApp', 'x64');
  const original = fs.readFileSync(path.join(application.root, runtime.receiptName));
  const receipt = JSON.parse(original);
  receipt.files.find(file => file.path === 'node.dll').sha256 = '0'.repeat(64);
  fs.writeFileSync(path.join(application.root, runtime.receiptName), id.encoded(receipt));
  assert.throws(() => runtime.verifyDirectory(application.root), /runtime|identity/i);
  assert.throws(() => id.payload(application.root, 'ReceiptApp'), /runtime|identity/i);
  assert.throws(() => bundle.pack(path.join(root, 'rejected'), [application]));
  assert.equal(fs.existsSync(path.join(root, 'rejected')), false);
});

test('missing receipt blob and corrupt DLL blob refuse verification and publish no restored directory', t => {
  const root = fixture(t), application = app(path.join(root, 'app'), 'BlobApp', 'x64');
  const output = path.join(root, 'bundle');
  bundle.pack(output, [application]);
  const receipt = application.report.payload.files.find(file => file.path === runtime.receiptName);
  const receiptFile = path.join(output, 'blobs', receipt.sha256), saved = fs.readFileSync(receiptFile);
  fs.unlinkSync(receiptFile);
  assert.throws(() => bundle.verify(output));
  assert.throws(() => bundle.restore(output, path.join(root, 'missing')));
  assert.equal(fs.existsSync(path.join(root, 'missing')), false);
  fs.writeFileSync(receiptFile, saved);
  const kernel = application.report.payload.files.find(file => file.path === 'node.dll');
  const kernelFile = path.join(output, 'blobs', kernel.sha256);
  const descriptor = fs.openSync(kernelFile, 'r+');
  fs.writeSync(descriptor, Buffer.from('CORRUPT'), 0, 7, 0); fs.closeSync(descriptor);
  assert.throws(() => bundle.verify(output), /content differs/i);
  assert.throws(() => bundle.restore(output, path.join(root, 'corrupt')));
  assert.equal(fs.existsSync(path.join(root, 'corrupt')), false);
});

test('PowerShell keeps default independent ZIPs and optionally publishes a verified deduplicated bundle', {
  skip: process.platform !== 'win32' || !fs.existsSync(path.join(process.env.ProgramFiles || '', '7-Zip/7z.exe')),
}, t => {
  const directory = fixture(t), zip = path.join(process.env.ProgramFiles, '7-Zip/7z.exe');
  function packageFixture(name) {
    const root = path.join(directory, name), internal = path.join(root, 'tools/internal');
    fs.mkdirSync(internal, {recursive: true});
    put(root, 'VERSION', '0.5.0\n');
    fs.copyFileSync(path.join(sdk, 'tools/Package-Applications.ps1'), path.join(root, 'tools/Package-Applications.ps1'));
    for (const helper of ['size-report.cjs', 'delivery-identity.cjs', 'runtime-bundle.cjs', 'runtime-deployment.cjs', 'sdk-version.cjs', 'compression-profiles.cjs'])
      fs.copyFileSync(path.join(sdk, 'tools/internal', helper), path.join(internal, helper));
    put(root, 'tools/internal/build-evidence.cjs', `
      const fs = require('node:fs'), path = require('node:path'), id = require('./delivery-identity.cjs');
      const [, architecture, configuration, target, stage] = process.argv.slice(2);
      for (const [name, content] of [[target + '.exe', 'SYNTHETIC EXE'], ['resources/index.html', '<main>shared</main>']]) {
        const file = path.join(stage, name); fs.mkdirSync(path.dirname(file), {recursive:true}); fs.writeFileSync(file, content);
      }
      const payload = id.payload(stage, target, 'webview2');
      console.log(JSON.stringify({schema:'soui.application-size/v1',version:'0.5.0',target,architecture,configuration,
        rendererBackend:'webview2',webTarget:null,buildId:'inert-stage-'+target,sourceSha256:id.hash('fixture'),
        payload,categories:{executable:13,kernel:0,libraries:0,web:19},releaseReady:false}));
    `);
    return root;
  }
  const run = (root, args = []) => spawnSync('powershell.exe', ['-NoProfile', '-ExecutionPolicy', 'Bypass', '-File',
    path.join(root, 'tools/Package-Applications.ps1'), ...args], {encoding: 'utf8', windowsHide: true,
    env: {...process.env, PSModulePath: path.join(process.env.SystemRoot || 'C:/Windows', 'System32/WindowsPowerShell/v1.0/Modules')}});
  const normal = packageFixture('legacy'), normalRun = run(normal);
  assert.equal(normalRun.status, 0, normalRun.stdout + normalRun.stderr);
  const normalArchives = fs.readdirSync(path.join(normal, 'dist')).filter(name => name.endsWith('.zip'));
  assert.equal(normalArchives.length, 2);
  assert.equal(normalArchives.some(name => name.includes('.bundle.')), false);
  const deduplicated = packageFixture('deduplicated'), packaged = run(deduplicated, ['-Bundle']);
  assert.equal(packaged.status, 0, packaged.stdout + packaged.stderr);
  const archives = fs.readdirSync(path.join(deduplicated, 'dist')).filter(name => name.endsWith('.zip'));
  assert.equal(archives.length, 1);
  const archive = path.join(deduplicated, 'dist', archives[0]);
  const size = id.readJson(archive + '.json');
  assert.equal(size.schema, 'soui.application-bundle-size/v1');
  assert.equal(size.releaseReady, false);
  assert.equal(size.archive.bytes, fs.statSync(archive).size);
  assert.equal(size.archive.sha256, id.hash(fs.readFileSync(archive)));
  const extracted = path.join(directory, 'extracted');
  const extract = spawnSync(zip, ['x', archive, '-o' + extracted, '-y'], {encoding:'utf8', windowsHide:true});
  assert.equal(extract.status, 0, extract.stdout + extract.stderr);
  const verified = bundle.verify(extracted);
  assert.equal(verified.applications.length, 2);
  assert.ok(verified.savedUncompressedBytes > 0);
  assert.deepEqual(size.totals, id.readJson(path.join(extracted, 'manifest.json')).totals);
  const restored = path.join(directory, 'restored');
  bundle.restore(extracted, restored);
  for (const application of id.readJson(path.join(extracted, 'manifest.json')).applications)
    assert.equal(id.verifyPayload(path.join(restored, application.name), application.report.target,
      application.report.payload, 'webview2').sha256, application.report.payload.sha256);
  const budget = path.join(directory, 'budget.json'); fs.writeFileSync(budget, JSON.stringify({totalBytes: 0}));
  for (const mode of [[], ['-Bundle']]) {
    const over = packageFixture(mode.length ? 'over-bundle' : 'over-zip');
    const rejected = run(over, [...mode, '-SizeBudgetPath', budget]);
    assert.notEqual(rejected.status, 0);
    assert.deepEqual(fs.readdirSync(path.join(over, 'dist')), []);
    const analyses = fs.readdirSync(path.join(over, 'out/package')).filter(name => name.endsWith('.size-analysis.json'));
    assert.equal(analyses.length, 1);
    assert.equal(id.readJson(path.join(over, 'out/package', analyses[0])).budget.passed, false);
  }
});
