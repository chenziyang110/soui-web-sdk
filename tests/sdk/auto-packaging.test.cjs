'use strict';
// Filesystem/contract fixtures only. Synthetic images are never loaded or run;
// real two-backend execution is required separately by Test-Auto.ps1.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const id = require('../../tools/internal/delivery-identity.cjs');
const targets = require('../../tools/internal/web-targets.cjs');
const profiles = require('../../tools/internal/web-build-profile.cjs');
const deploy = require('../../tools/internal/deploy-resources.cjs');
const runtime = require('../../tools/internal/runtime-deployment.cjs');
const evidence = require('../../tools/internal/build-evidence.cjs');
const bundles = require('../../tools/internal/runtime-bundle.cjs');
const sdk = require('../../tools/sdk/soui-sdk.cjs');
const kit = require('../../tools/internal/devkit-command.cjs');
const standard = require('../../tools/internal/standard-build.cjs');
const sdkRoot = path.resolve(__dirname, '../..');

function fixture(t) {
  const owner = fs.realpathSync(os.tmpdir());
  const root = fs.realpathSync(fs.mkdtempSync(path.join(owner, 'soui-auto-package-')));
  t.after(() => {
    assert.equal(path.dirname(root), owner); assert.ok(path.basename(root).startsWith('soui-auto-package-'));
    assert.equal(id.safePath(root), root); fs.rmSync(root, {recursive: true, force: true});
  });
  const put = (name, data) => {
    const full = id.safePath(root, name); fs.mkdirSync(path.dirname(full), {recursive: true}); fs.writeFileSync(full, data); return full;
  };
  const metadata = {schema: 'soui.application/v1', nativeTarget: 'AutoApp', runtimeProfile: 'auto', webTargets: {
    default: 'modern', targets: ['modern', 'compatibility'].map(name => ({name, entry: `web/${name}/index.html`, publicAssets: []}))}};
  put('project.json', id.encoded(metadata));
  put('VERSION', '0.6.0-preview.1\n');
  put('sdk/include/soui/version.h', '#define SOUI_SDK_VERSION_STRING "0.6.0-preview.1"\n');
  put('sdk/web/package.json', JSON.stringify({version: '0.6.0-preview.1'}));
  put('sdk/web/package-lock.json', JSON.stringify({version: '0.6.0-preview.1', packages: {'': {version: '0.6.0-preview.1'}}}));
  put('sdk/web/dist/index.js', 'export {};');
  put('THIRD_PARTY_NOTICES.md', 'Synthetic legal materials.'); put('third_party/licenses/license.txt', 'Fixture license.');
  for (const name of ['modern', 'compatibility']) put(`web/${name}/index.html`, `<main>${name}</main>`);
  const plan = targets.resolve(root);
  for (const target of plan.targets) {
    put(target.outDir + '/index.html', `<main>${target.name}</main>`);
    put(target.outDir + '/assets/app.js', `window.profile = '${target.name}';`);
    const {plugin} = profiles.buildProfile('distribution');
    plugin.configResolved({root: path.join(root, 'web'), build: {outDir: path.relative(path.join(root, 'web'), path.join(root, target.outDir))}});
    plugin.closeBundle();
    targets.createManifest(path.join(root, target.outDir), {...target, configurationSha256: plan.configurationSha256});
  }
  const manifest = id.readJson(path.join(sdkRoot, 'tools/internal/runtime-files.json'));
  for (const arch of ['x86', 'x64']) manifest.architectures[arch] = Object.keys(manifest.components).map((component, index) => {
    const bytes = Buffer.alloc(160, index); bytes.write('MZ'); bytes.writeUInt32LE(64, 0x3c); bytes.write('PE\0\0', 64);
    bytes.writeUInt16LE(arch === 'x64' ? 0x8664 : 0x14c, 68);
    const name = component === 'miniblink' ? 'node.dll' : component + '.dll', source = `third_party/prebuilt/${arch}/fixture/${name}`;
    put(source, bytes); return {component, source, path: name, bytes: bytes.length, sha256: id.hash(bytes)};
  });
  put('tools/internal/runtime-files.json', id.encoded(manifest));
  const app = path.join(root, 'out/x64/bin/Release/AutoApp');
  put('out/x64/bin/Release/AutoApp/AutoApp.exe', 'SYNTHETIC: never executed');
  put('out/x64/CMakeCache.txt', 'SOUI_BUILD_WEBVIEW2:BOOL=ON\nSOUI_BUILD_AUTO_HOST:BOOL=ON\n');
  put('out/x64/sdk/renderers/webview2/Release/soui_webview2.lib', 'synthetic modern archive');
  put('out/x64/sdk/renderers/webview2/Release/soui_auto_host.lib', 'synthetic auto archive');
  return {root, put, metadata, manifest, app, plan};
}
function build(f) {
  evidence.start(f.root, 'x64', 'Release');
  evidence.webEvidence(f.root, 'x64', 'Release', false, true);
  evidence.deployWeb(f.root, 'x64', 'Release');
  runtime.deploy(f.root, f.app, 'x64', 'auto', {projectRoot: f.root});
  evidence.finish(f.root, 'x64', 'Release');
  return evidence.verifyBuild(f.root, 'x64', 'Release');
}

test('auto is explicit and old command defaults remain unchanged', () => {
  const create = kit.parseArgs(['create', '--name', 'MyAuto', '--output-root', 'C:/MyAuto', '--profile', 'auto']);
  assert.ok(kit.planCommand(create, 'C:/sdk', 'powershell.exe').args.includes('auto'));
  const pack = kit.planCommand(kit.parseArgs(['package', '--kind', 'applications', '--profile', 'auto']), 'C:/sdk', 'powershell.exe');
  assert.equal(pack.args[pack.args.indexOf('-Profile') + 1], 'auto');
  assert.equal(kit.parseArgs(['create', '--name', 'MyApp', '--output-root', 'C:/MyApp']).profile, 'compatibility');
  assert.ok(!kit.planCommand(kit.parseArgs(['package', '--kind', 'applications']), 'C:/sdk', 'powershell.exe').args.includes('-Profile'));
  const value = standard.parseArgs(['--with-auto-host']);
  assert.ok(standard.buildArguments('C:/sdk', value).includes('-WithAutoHost'));
  assert.ok(!standard.buildArguments('C:/sdk', standard.parseArgs([])).includes('-WithAutoHost'));
});

test('auto deployment publishes separate target inventories and an external WebView2 dependency', t => {
  const f = fixture(t), report = build(f);
  assert.equal(report.autoHost, true);
  assert.ok(report.optionalLibraries.autoHost);
  assert.equal(fs.readFileSync(path.join(f.app, 'resources/web-modern/index.html'), 'utf8'), '<main>modern</main>');
  assert.equal(fs.readFileSync(path.join(f.app, 'resources/web-compat/index.html'), 'utf8'), '<main>compatibility</main>');
  assert.equal(fs.existsSync(path.join(f.app, 'resources/index.html')), false);
  const receipt = runtime.verifyDirectory(f.app);
  assert.deepEqual(receipt.externalWebView2, {deployment: 'external', selection: 'installed-or-explicit-fixed', required: false, fallback: 'miniblink', download: false});
  assert.deepEqual(receipt.files, runtime.resolve(f.root, 'x64', 'miniblink').files);
  assert.throws(() => evidence.stageApp(f.root, 'x64', 'Release', 'AutoApp', path.join(f.root, 'stage')), /auto-smoke/);
  assert.equal(fs.existsSync(path.join(f.root, 'stage')), false);
});

test('missing compatibility output cannot partially replace a previous deployment', t => {
  const f = fixture(t); build(f);
  const before = id.snapshot(f.app, id.walk(f.app)).sha256;
  fs.unlinkSync(path.join(f.root, 'web/dist/compatibility/index.html'));
  assert.throws(() => deploy.replaceAuto(f.root, f.app, f.root), /missing index|E_WEB_TARGET/);
  assert.equal(id.snapshot(f.app, id.walk(f.app)).sha256, before);
});

test('mixed target roots, swapped manifests and absent MiniBlink are rejected', t => {
  const f = fixture(t); build(f);
  const bad = f.put('out/x64/bin/Release/AutoApp/resources/index.html', 'unexpected mixed root');
  assert.throws(() => id.payload(f.app, 'AutoApp', 'auto'), /mixed or unexpected/); fs.unlinkSync(bad);
  const modernMarker = path.join(f.app, 'resources/web-modern/.soui-web-target.json');
  const original = fs.readFileSync(modernMarker);
  fs.copyFileSync(path.join(f.app, 'resources/web-compat/.soui-web-target.json'), modernMarker);
  assert.throws(() => id.payload(f.app, 'AutoApp', 'auto'), /E_WEB_TARGET/); fs.writeFileSync(modernMarker, original);
  fs.unlinkSync(path.join(f.app, 'node.dll'));
  assert.throws(() => id.payload(f.app, 'AutoApp', 'auto'), /Runtime file missing/);
});

test('auto receipt cannot omit its fallback or silently change external runtime policy', t => {
  const f = fixture(t); build(f);
  const receipt = runtime.verifyDirectory(f.app);
  delete receipt.externalWebView2;
  f.put('out/x64/bin/Release/AutoApp/.soui-runtime.json', id.encoded(receipt));
  assert.throws(() => runtime.verifyDirectory(f.app), /external WebView2/);
});

test('auto build cannot accept a missing optional backend library', t => {
  const f = fixture(t); build(f);
  fs.unlinkSync(path.join(f.root, 'out/x64/sdk/renderers/webview2/Release/soui_auto_host.lib'));
  assert.throws(() => evidence.verifyBuild(f.root, 'x64', 'Release'), /ENOENT/);
});

test('byte-based bundles preserve both auto resource roots and restore exact runtime files', t => {
  const f = fixture(t), built = build(f);
  const report = {schema: 'soui.application-size/v1', version: built.version, target: 'AutoApp',
    architecture: 'x64', configuration: 'Release', rendererBackend: 'auto', buildId: built.buildId,
    sourceSha256: built.source.sha256, releaseReady: false, payload: built.payloads.AutoApp};
  const archive = path.join(f.root, 'bundle'), restored = path.join(f.root, 'restored');
  bundles.pack(archive, [{name: 'AutoApp', root: f.app, report}]);
  bundles.restore(archive, restored);
  const payload = id.payload(path.join(restored, 'AutoApp'), 'AutoApp', 'auto');
  assert.equal(payload.sha256, report.payload.sha256);
});

test('full devkit explicitly records the optional auto template and both Web outputs', t => {
  const f = fixture(t), autoProject = path.join(f.root, 'templates/auto-app');
  fs.mkdirSync(autoProject, {recursive: true});
  fs.cpSync(path.join(f.root, 'web'), path.join(autoProject, 'web'), {recursive: true});
  f.put('templates/auto-app/project.json', id.encoded({...f.metadata, nativeTarget: 'SouiAutoStarter'}));
  fs.unlinkSync(path.join(f.root, 'project.json'));
  for (const [target, source, backend] of [['SouiStarter', 'templates/desktop-app', 'miniblink'],
    ['SouiWorkbench', 'demos/workbench', 'miniblink'], ['SouiModernStarter', 'templates/modern-app', 'webview2'],
    ['SouiAutoStarter', 'templates/auto-app', 'auto']]) {
    f.put(`out/x64/bin/Release/${target}/${target}.exe`, 'SYNTHETIC never executed');
    if (backend !== 'auto') f.put(source + '/web/dist/index.html', '<main>Legacy payload</main>');
    runtime.deploy(f.root, path.join(f.root, `out/x64/bin/Release/${target}`), 'x64', backend,
      {projectRoot: path.join(f.root, source)});
  }
  const selection = {autoHost: 'enabled'};
  evidence.start(f.root, 'x64', 'Release', false, selection);
  evidence.webEvidence(f.root, 'x64', 'Release', false, true, selection);
  evidence.deployWeb(f.root, 'x64', 'Release', false, selection);
  evidence.finish(f.root, 'x64', 'Release', false, selection);
  const built = evidence.verifyBuild(f.root, 'x64', 'Release');
  assert.ok(built.optionalPayloads.SouiAutoStarter);
  assert.ok(built.optionalPayloads.SouiModernStarter);
  assert.deepEqual(Object.keys(built.payloads), ['SouiStarter', 'SouiWorkbench']);
  const web = id.readJson(path.join(f.root, 'out/web-applications.json'));
  assert.equal(web.applications.filter(row => row.root.startsWith('templates/auto-app/')).length, 2);
  fs.unlinkSync(path.join(f.root, 'templates/auto-app/web/dist/compatibility/index.html'));
  assert.throws(() => evidence.verifyBuild(f.root, 'x64', 'Release'), /E_WEB_TARGET/);
});

test('auto resource configuration cannot reduce the application to a single target', t => {
  const f = fixture(t);
  f.metadata.webTargets.targets.pop(); f.put('project.json', id.encoded(f.metadata));
  assert.throws(() => deploy.replaceAuto(f.root, f.app, f.root), /distinct modern and compatibility/);
});

test('auto SDK selection requires both actual source backends and fallback architecture files', () => {
  const paths = ['sdk/include/soui/host/webview2_host.h', 'sdk/renderers/webview2/CMakeLists.txt', 'tools/internal/Restore-WebView2.ps1',
    'sdk/include/soui/host/auto_webview_host.h', 'sdk/renderers/auto/auto_webview_host.cpp', 'sdk/include/soui/host/soui_miniblink_host.h',
    'tools/internal/runtime-files.json', 'tools/internal/auto-smoke.cjs', 'tools/Test-Auto.ps1',
    'third_party/prebuilt/x86/miniblink/node.dll', 'third_party/prebuilt/x64/miniblink/node.dll'];
  sdk.requireProfile({files: paths.map(path => ({path}))}, 'auto');
  for (const missing of paths) assert.throws(() => sdk.requireProfile({files: paths.filter(path => path !== missing).map(path => ({path}))}, 'auto'), /does not provide/);
  sdk.requireProfile({files: []});
});
