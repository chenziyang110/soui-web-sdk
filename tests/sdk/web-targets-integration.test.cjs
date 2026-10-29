'use strict';

// These filesystem fixtures only exercise publication and delivery contracts.
// Their synthetic EXE/DLL bytes are never compiled, loaded or executed.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {spawnSync} = require('node:child_process');
const identity = require('../../tools/internal/delivery-identity.cjs');
const profiles = require('../../tools/internal/web-build-profile.cjs');
const targets = require('../../tools/internal/web-targets.cjs');
const evidence = require('../../tools/internal/build-evidence.cjs');

const nativeTarget = 'TargetApp';
const version = '0.5.0-preview.1';
const fixturePrefix = 'soui-web-targets-integration-';
const selection = {webProfile: 'distribution'};

function removeOwned(directory, owner) {
  const absolute = path.resolve(directory), root = path.resolve(owner);
  const relative = path.relative(root, absolute);
  assert.ok(relative === '' || (!path.isAbsolute(relative) && relative !== '..' &&
    !relative.startsWith('..' + path.sep)), 'Cleanup stays inside its created fixture');
  assert.equal(identity.safePath(absolute), absolute, 'Cleanup cannot follow linked paths');
  fs.rmSync(absolute, {recursive: true, force: true});
}

function fixture(t, configured = true) {
  const temporary = fs.realpathSync(os.tmpdir());
  const owner = fs.realpathSync(fs.mkdtempSync(path.join(temporary, fixturePrefix)));
  let root = path.join(owner, 'original');
  fs.mkdirSync(root);
  t.after(() => {
    assert.equal(path.dirname(owner), temporary);
    assert.ok(path.basename(owner).startsWith(fixturePrefix));
    assert.equal(fs.realpathSync(owner), owner);
    removeOwned(owner, owner);
  });
  const put = (name, data) => {
    const file = identity.safePath(root, name);
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, data);
    return file;
  };
  const metadata = {schema: 'soui.application/v1', nativeTarget, runtimeProfile: 'compatibility'};
  if (configured) metadata.webTargets = {default: 'main', targets: [
    {name: 'main', entry: 'web/main/index.html', publicAssets: ['images/main.png']},
    {name: 'pe', entry: 'web/pe/index.html', publicAssets: ['images/account-key.png']},
  ]};
  put('project.json', identity.encoded(metadata));
  put('VERSION', version + '\n');
  put('sdk/include/soui/version.h', `#define SOUI_SDK_VERSION_STRING "${version}"\n`);
  put('sdk/web/package.json', JSON.stringify({version}));
  put('sdk/web/package-lock.json', JSON.stringify({version, packages: {'': {version}}}));
  put('sdk/web/src/index.ts', 'export const synthetic = true;\n');
  put('sdk/web/dist/index.js', 'export const synthetic = true;\n');
  put('sdk/web/dist/index.d.ts', 'export declare const synthetic: boolean;\n');
  put('web/package.json', JSON.stringify({private: true}));
  put('web/index.html', '<!doctype html><main>Legacy entry</main>\n');
  put('web/main/index.html', '<!doctype html><main>Main entry</main>\n');
  put('web/pe/index.html', '<!doctype html><main>PE entry</main>\n');
  put('web/public/images/main.png', Buffer.from('SYNTHETIC main image\0'));
  put('web/public/images/account-key.png', Buffer.from('SYNTHETIC PE image\0'));
  put('LICENSE.md', '# Synthetic first-party license\n');
  put('THIRD_PARTY_NOTICES.md', '# Synthetic legal notices\n');
  put('third_party/licenses/fixture-LICENSE.txt', 'Synthetic legal bytes.\n');
  put(`out/x64/bin/Release/${nativeTarget}/${nativeTarget}.exe`, Buffer.from('SYNTHETIC EXE: never executed\0'));
  put(`out/x64/bin/Release/${nativeTarget}/node.dll`, Buffer.from('SYNTHETIC DLL: never loaded\0'));
  return {get root() { return root; }, put, metadata,
    get app() { return path.join(root, `out/x64/bin/Release/${nativeTarget}`); },
    get webFile() { return path.join(root, 'out/web-applications.json'); },
    get buildFile() { return path.join(root, 'out/x64/build-Release.json'); },
    relocate() {
      const moved = path.join(owner, 'moved project 中文');
      fs.renameSync(root, moved);
      root = moved;
    }};
}

function publish(f) {
  const plan = targets.resolve(f.root);
  for (const target of plan.targets) {
    const directory = path.join(f.root, target.outDir);
    removeOwned(directory, f.root);
    f.put(target.outDir + '/index.html', '<!doctype html><script src="./assets/app.js"></script>\n');
    f.put(target.outDir + '/assets/app.js', `window.targetName = ${JSON.stringify(target.name || 'legacy')};\n`);
    for (const asset of target.publicAssets) f.put(target.outDir + '/' + asset,
      fs.readFileSync(path.join(f.root, 'web/public', asset)));
    const {plugin} = profiles.buildProfile('distribution');
    plugin.configResolved({root: path.join(f.root, 'web'), build: {outDir: path.relative(path.join(f.root, 'web'), directory)}});
    plugin.closeBundle();
    if (plan.configured) targets.createManifest(directory,
      {...target, configurationSha256: plan.configurationSha256});
  }
  return plan;
}

function record(f) {
  evidence.start(f.root, 'x64', 'Release', false, selection);
  return evidence.webEvidence(f.root, 'x64', 'Release', false, true, selection);
}
function deploy(f) { return evidence.deployWeb(f.root, 'x64', 'Release', false, selection); }
function finish(f) {
  evidence.finish(f.root, 'x64', 'Release', false, selection);
  return evidence.verifyBuild(f.root, 'x64', 'Release', false, selection);
}
function deployed(f) {
  const root = path.join(f.app, 'resources');
  return identity.snapshot(root, identity.walk(root));
}

test('legacy project without webTargets retains record, SkipWeb, deployment and accepted build behavior', t => {
  const f = fixture(t, false);
  assert.equal(publish(f).configured, false);
  record(f);
  assert.equal(evidence.webEvidence(f.root, 'x64', 'Release', false, false, selection).webVerified, true);
  assert.deepEqual(deploy(f).targets, [nativeTarget]);
  const build = finish(f);
  assert.equal(build.nativeBuild, 'passed');
  assert.equal(build.releaseReady, false);
  const web = identity.readJson(f.webFile);
  assert.deepEqual(web.applications.map(application => application.root), ['web/dist']);
  assert.equal(fs.existsSync(path.join(f.app, 'resources', targets.manifestName)), false);
  assert.equal(evidence.verifyAcceptedBuild(f.root, 'x64', 'Release').build.buildId, build.buildId);
});

test('both targets enter Web evidence while only the declared default enters the application', t => {
  const f = fixture(t);
  const plan = publish(f);
  for (const target of plan.targets) assert.equal(targets.verifyTargetDirectory(path.join(f.root, target.outDir),
    {...target, configurationSha256: plan.configurationSha256}, {requireManifest: true}).webTarget, target.name);
  record(f);
  const web = identity.readJson(f.webFile);
  assert.deepEqual(web.applications.map(application => application.root), ['web/dist/main', 'web/dist/pe']);
  assert.ok(web.artifacts.files.some(file => file.path === 'web/dist/pe/images/account-key.png'));
  assert.ok(web.artifacts.files.some(file => file.path === 'web/dist/main/' + targets.manifestName));
  deploy(f);
  assert.equal(fs.existsSync(path.join(f.app, 'resources/images/main.png')), true);
  assert.equal(fs.existsSync(path.join(f.app, 'resources/images/account-key.png')), false);
  assert.equal(identity.readJson(path.join(f.app, 'resources', targets.manifestName)).target, 'main');
  const build = finish(f);
  assert.ok(build.payloads[nativeTarget].files.some(file => file.path === 'resources/' + targets.manifestName));
  assert.equal(build.releaseReady, false);
});

const corruptions = {
  'deleted secondary output'(f) { removeOwned(path.join(f.root, 'web/dist/pe'), f.root); },
  'swapped target manifests'(f) {
    const main = path.join(f.root, 'web/dist/main', targets.manifestName);
    const pe = path.join(f.root, 'web/dist/pe', targets.manifestName);
    const mainBytes = fs.readFileSync(main);
    fs.writeFileSync(main, fs.readFileSync(pe));
    fs.writeFileSync(pe, mainBytes);
  },
  'undeclared extra image'(f) { f.put('web/dist/pe/images/extra.png', Buffer.from('UNDECLARED\0')); },
  'changed target configuration'(f) {
    f.metadata.webTargets.default = 'pe';
    f.put('project.json', identity.encoded(f.metadata));
  },
};

for (const [description, corrupt] of Object.entries(corruptions)) {
  test(`${description} refuses SkipWeb, finish and redeployment while preserving previous resources`, t => {
    const f = fixture(t);
    publish(f); record(f); deploy(f);
    const before = deployed(f);
    corrupt(f);
    assert.throws(() => evidence.webEvidence(f.root, 'x64', 'Release', false, false, selection));
    assert.throws(() => evidence.finish(f.root, 'x64', 'Release', false, selection));
    assert.throws(() => deploy(f));
    assert.equal(identity.readJson(f.buildFile).nativeBuild, 'pending');
    assert.deepEqual(deployed(f), before);
    assert.deepEqual(fs.readdirSync(f.app).filter(name => name.startsWith('.resources-')), []);
  });
  test(`${description} invalidates an already accepted build`, t => {
    const f = fixture(t);
    publish(f); record(f); deploy(f); finish(f);
    const before = deployed(f);
    corrupt(f);
    assert.throws(() => evidence.verifyBuild(f.root, 'x64', 'Release', false, selection));
    assert.throws(() => evidence.verifyAcceptedBuild(f.root, 'x64', 'Release'));
    assert.deepEqual(deployed(f), before);
  });
}

test('changing the default after a fresh publication replaces owned resources and removes the previous target', t => {
  const f = fixture(t);
  publish(f); record(f); deploy(f); finish(f);
  const nativeHash = identity.hash(fs.readFileSync(path.join(f.app, nativeTarget + '.exe')));
  f.metadata.webTargets.default = 'pe';
  f.put('project.json', identity.encoded(f.metadata));
  publish(f); record(f); deploy(f); finish(f);
  assert.equal(identity.readJson(path.join(f.app, 'resources', targets.manifestName)).target, 'pe');
  assert.equal(fs.existsSync(path.join(f.app, 'resources/images/main.png')), false);
  assert.equal(fs.existsSync(path.join(f.app, 'resources/images/account-key.png')), true);
  assert.equal(identity.hash(fs.readFileSync(path.join(f.app, nativeTarget + '.exe'))), nativeHash);
});

test('target configuration, accepted evidence and resource manifests remain valid after moving the whole project', t => {
  const f = fixture(t);
  const original = publish(f);
  record(f); deploy(f);
  const build = finish(f);
  const before = deployed(f);
  f.relocate();
  assert.deepEqual(targets.resolve(f.root), original);
  assert.equal(evidence.webEvidence(f.root, 'x64', 'Release', false, false, selection).webVerified, true);
  assert.equal(evidence.verifyAcceptedBuild(f.root, 'x64', 'Release').build.buildId, build.buildId);
  assert.deepEqual(deployed(f), before);
  // A new build at the new location must retain default selection as well.
  record(f); deploy(f); finish(f);
  assert.deepEqual(deployed(f), before);
});

test('direct CMake deployment CLI retains legacy project compatibility', t => {
  const f = fixture(t, false);
  publish(f);
  const helper = path.resolve(__dirname, '../../tools/internal/deploy-resources.cjs');
  const result = spawnSync(process.execPath, [helper, path.join(f.root, 'web/dist'), f.app, f.root, f.root],
    {encoding: 'utf8'});
  assert.equal(result.status, 0, result.stderr);
  assert.equal(fs.existsSync(path.join(f.app, 'resources/index.html')), true);
});

test('direct CMake deployment CLI requires configured target manifest before replacing old resources', t => {
  const f = fixture(t);
  publish(f); record(f); deploy(f);
  const before = deployed(f);
  const source = path.join(f.root, 'web/dist/main');
  fs.unlinkSync(path.join(source, targets.manifestName));
  const helper = path.resolve(__dirname, '../../tools/internal/deploy-resources.cjs');
  const result = spawnSync(process.execPath, [helper, source, f.app, f.root, f.root], {encoding: 'utf8'});
  assert.notEqual(result.status, 0);
  assert.match(result.stderr, /E_WEB_TARGET_MANIFEST_REQUIRED/);
  assert.deepEqual(deployed(f), before);
  assert.deepEqual(fs.readdirSync(f.app).filter(name => name.startsWith('.resources-')), []);
});
