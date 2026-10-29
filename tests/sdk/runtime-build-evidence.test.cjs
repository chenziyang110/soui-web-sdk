'use strict';
// These files exercise build/deployment contracts; synthetic PE images are never executed.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const id = require('../../tools/internal/delivery-identity.cjs');
const evidence = require('../../tools/internal/build-evidence.cjs');
const runtime = require('../../tools/internal/runtime-deployment.cjs');

function fixture(t, explicit = true) {
  const parent = fs.realpathSync(os.tmpdir());
  const root = fs.mkdtempSync(path.join(parent, 'soui-runtime-build-'));
  t.after(() => {
    assert.equal(path.dirname(root), parent);
    assert.ok(path.basename(root).startsWith('soui-runtime-build-'));
    id.safePath(root);
    fs.rmSync(root, {recursive: true, force: true});
  });
  const put = (name, bytes) => {
    const file = id.safePath(root, name);
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, bytes);
  };
  const defaults = JSON.parse(fs.readFileSync(path.join(__dirname, '../../tools/internal/runtime-files.json'), 'utf8'));
  const manifest = {...defaults, architectures: {}};
  const sources = {'crt': 'runtime/crt.dll', 'soui-core': 'soui/bin/core.dll',
    'soui-gdi': 'soui/bin/gdi.dll', 'soui-resources': 'soui/bin/resource.dll', 'miniblink': 'miniblink/node.dll'};
  for (const architecture of ['x86', 'x64']) {
    manifest.architectures[architecture] = Object.entries(sources).map(([component, name]) => {
      const bytes = Buffer.alloc(160); bytes.write('MZ'); bytes.writeUInt32LE(64, 0x3c);
      bytes.write('PE\0\0', 64); bytes.writeUInt16LE(architecture === 'x64' ? 0x8664 : 0x14c, 68);
      bytes.write(component, 90);
      const source = `third_party/prebuilt/${architecture}/${name}`;
      put(source, bytes);
      return {source, path: path.posix.basename(name), bytes: bytes.length, sha256: id.hash(bytes), component};
    });
  }
  put('tools/internal/runtime-files.json', id.encoded(manifest));
  put('VERSION', '0.5.0\n');
  put('sdk/include/soui/version.h', '#define SOUI_SDK_VERSION_STRING "0.5.0"\n');
  put('sdk/web/package.json', JSON.stringify({version: '0.5.0'}));
  put('sdk/web/package-lock.json', JSON.stringify({version: '0.5.0', packages: {'': {version: '0.5.0'}}}));
  put('sdk/web/dist/index.js', 'export {};');
  put('web/dist/index.html', '<!doctype html><main>runtime fixture</main>');
  put('THIRD_PARTY_NOTICES.md', 'Synthetic notices');
  put('third_party/licenses/fixture.txt', 'Synthetic license');
  put('project.json', JSON.stringify({nativeTarget: 'RuntimeApp', runtimeProfile: 'compatibility',
    ...(explicit ? {runtimeComponents: defaults.backends.miniblink.default} : {})}));
  const app = path.join(root, 'out/x64/bin/Release/RuntimeApp');
  put('out/x64/bin/Release/RuntimeApp/RuntimeApp.exe', 'SYNTHETIC: never executed');
  function begin() {
    evidence.start(root, 'x64', 'Release');
    evidence.webEvidence(root, 'x64', 'Release', false, true);
    evidence.deployWeb(root, 'x64', 'Release');
  }
  return {root, app, put, begin};
}

test('explicit runtime selection is bound to the accepted build and full payload', t => {
  const f = fixture(t); f.begin();
  runtime.deploy(f.root, f.app, 'x64', 'miniblink', {projectRoot: f.root});
  evidence.finish(f.root, 'x64', 'Release');
  const build = evidence.verifyBuild(f.root, 'x64', 'Release');
  assert.ok(build.payloads.RuntimeApp.files.some(file => file.path === runtime.receiptName));
  fs.unlinkSync(path.join(f.app, runtime.receiptName));
  assert.throws(() => evidence.verifyBuild(f.root, 'x64', 'Release'), /deployment receipt/);
});

test('explicit components cannot finish without a runtime receipt', t => {
  const f = fixture(t); f.begin();
  f.put('out/x64/bin/Release/RuntimeApp/node.dll', 'unverified');
  assert.throws(() => evidence.finish(f.root, 'x64', 'Release'), /deployment receipt/);
});

test('another architecture and a different valid component selection cannot finish', t => {
  const f = fixture(t); f.begin();
  runtime.deploy(f.root, f.app, 'x86', 'miniblink', {projectRoot: f.root});
  assert.throws(() => evidence.finish(f.root, 'x64', 'Release'), /architecture or renderer/);
  runtime.deploy(f.root, f.app, 'x64', 'miniblink', {components: ['soui-core', 'miniblink']});
  assert.throws(() => evidence.finish(f.root, 'x64', 'Release'), /component selection/);
});

test('legacy custom application without runtime selection or receipt stays supported', t => {
  const f = fixture(t, false); f.begin();
  f.put('out/x64/bin/Release/RuntimeApp/node.dll', 'legacy runtime fixture');
  evidence.finish(f.root, 'x64', 'Release');
  assert.equal(evidence.verifyBuild(f.root, 'x64', 'Release').nativeBuild, 'passed');
});

function repositoryWorkbenchFixture(t) {
  const f = fixture(t, false);
  fs.unlinkSync(path.join(f.root, 'project.json'));
  f.put('LICENSE.md', 'Synthetic SDK license');
  for (const name of ['templates/desktop-app', 'templates/modern-app', 'demos/workbench'])
    f.put(name + '/web/dist/index.html', '<main>repository fixture</main>');
  const native = require('../../demos/workbench/tools/deploy-native-fixtures.cjs');
  f.put('demos/workbench/tools/deploy-native-fixtures.cjs',
    fs.readFileSync(path.resolve(__dirname, '../../demos/workbench/tools/deploy-native-fixtures.cjs')));
  const fixtureSource = path.join(f.root, 'demos/workbench/fixtures');
  for (const name of ['fileinsight-fixture', 'fileinsight-fixture-alt']) {
    fs.cpSync(path.resolve(__dirname, '../../demos/workbench/fixtures', name),
      path.join(fixtureSource, name), {recursive: true});
  }
  for (const target of ['SouiStarter', 'SouiWorkbench']) {
    f.put('out/x64/bin/Release/' + target + '/' + target + '.exe', 'SYNTHETIC: never executed');
    f.put('out/x64/bin/Release/' + target + '/node.dll', 'SYNTHETIC: never loaded');
  }
  const workbench = path.join(f.root, 'out/x64/bin/Release/SouiWorkbench');
  native.deploy(fixtureSource, workbench);
  return {...f, workbench};
}

test('application files: normal repository finish and verify bind Workbench data to its source owner', t => {
  const f = repositoryWorkbenchFixture(t); f.begin();
  evidence.finish(f.root, 'x64', 'Release');
  const build = evidence.verifyBuild(f.root, 'x64', 'Release');
  assert.equal(build.payloads.SouiWorkbench.applicationFiles.length, 10);
  assert.equal(Object.hasOwn(build.payloads.SouiStarter, 'applicationFiles'), false);
  const owner = path.join(f.workbench, '.soui-workbench-native-fixtures.json');
  fs.appendFileSync(owner, ' ');
  assert.throws(() => evidence.verifyBuild(f.root, 'x64', 'Release'), /ownership differs/);
});

test('application files: generated project never imports a repository Demo verifier implicitly', t => {
  const f = fixture(t, false);
  f.put('LICENSE.md', 'Synthetic SDK license');
  f.put('project.json', JSON.stringify({nativeTarget: 'SouiWorkbench', runtimeProfile: 'compatibility'}));
  f.put('demos/workbench/tools/deploy-native-fixtures.cjs', "throw new Error('Demo must not load');");
  f.put('out/x64/bin/Release/SouiWorkbench/SouiWorkbench.exe', 'SYNTHETIC: never executed');
  f.put('out/x64/bin/Release/SouiWorkbench/node.dll', 'SYNTHETIC: never loaded');
  f.begin();
  evidence.finish(f.root, 'x64', 'Release');
  const build = evidence.verifyBuild(f.root, 'x64', 'Release');
  assert.equal(Object.hasOwn(build.payloads.SouiWorkbench, 'applicationFiles'), false);
});
