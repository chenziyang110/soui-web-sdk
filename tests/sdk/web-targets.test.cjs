'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {pathToFileURL} = require('node:url');
const {createRequire} = require('node:module');
const {spawnSync} = require('node:child_process');
const targets = require('../../tools/internal/web-targets.cjs');
const profiles = require('../../tools/internal/web-build-profile.cjs');
const identity = require('../../tools/internal/delivery-identity.cjs');
const sdkRoot = path.resolve(__dirname, '../..');
const webModules = path.join(sdkRoot, 'templates/desktop-app/web/node_modules');

function fixture(t) {
  const owner = fs.realpathSync(os.tmpdir());
  const root = fs.realpathSync(fs.mkdtempSync(path.join(owner, 'soui-web-targets-')));
  t.after(() => {
    assert.equal(path.dirname(root), owner);
    assert.ok(path.basename(root).startsWith('soui-web-targets-'));
    assert.equal(identity.safePath(root), root);
    fs.rmSync(root, {recursive: true, force: true});
  });
  const put = (name, content) => {
    const file = identity.safePath(root, name);
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, content);
    return file;
  };
  const config = {default: 'main', targets: [
    {name: 'main', entry: 'web/main/index.html', publicAssets: ['images/logo.png']},
    {name: 'pe', entry: 'web/pe/index.html', publicAssets: ['images/logo.png', 'images/account-key.png']},
  ]};
  const configure = value => put('project.json', JSON.stringify({schema: 'soui.application/v1', webTargets: value}));
  put('web/main/index.html', '<html><body><script type="module" src="../src/main.ts"></script></body></html>');
  put('web/pe/index.html', '<html><body><script type="module" src="../src/pe.ts"></script></body></html>');
  put('web/src/main.ts', 'const value: string = "MAIN_ONLY"; window.fixture = value; fetch("./images/logo.png");\n');
  put('web/src/pe.ts', 'const value: string = "PE_ONLY"; window.fixture = value; fetch("./images/account-key.png");\n');
  put('web/public/images/logo.png', Buffer.from([0x89, 0x50, 0x4e, 0x47, 1]));
  put('web/public/images/account-key.png', Buffer.from([0x89, 0x50, 0x4e, 0x47, 2]));
  put('web/public/images/unused.png', Buffer.from([0x89, 0x50, 0x4e, 0x47, 3]));
  configure(config);
  return {root, put, config, configure};
}
function withEnvironment(values, action) {
  const before = Object.fromEntries(Object.keys(values).map(key => [key, process.env[key]]));
  for (const [key, value] of Object.entries(values)) {
    if (value === undefined) delete process.env[key]; else process.env[key] = value;
  }
  return Promise.resolve().then(action).finally(() => {
    for (const [key, value] of Object.entries(before)) {
      if (value === undefined) delete process.env[key]; else process.env[key] = value;
    }
  });
}
async function viteBuild(f, name, profile = 'distribution') {
  const {build} = await import(pathToFileURL(path.join(webModules, 'vite/dist/node/index.js')).href);
  return withEnvironment({SOUI_WEB_TARGET: name, SOUI_WEB_PROJECT_ROOT: f.root}, async () => {
    const configured = profiles.buildProfile(profile);
    await build({configFile: false, root: path.join(f.root, 'web'), base: './', logLevel: 'silent',
      plugins: [configured.plugin], build: {manifest: true, ...configured.build}});
  });
}

test('legacy projects preserve one web/dist output and the original Vite public-directory behavior', async t => {
  const f = fixture(t);
  f.put('project.json', JSON.stringify({schema: 'soui.application/v1'}));
  f.put('web/index.html', fs.readFileSync(path.join(f.root, 'web/main/index.html'), 'utf8').replace('../src', './src'));
  assert.deepEqual(targets.resolve(f.root), {configured: false, default: null, configurationSha256: null,
    targets: [{name: null, entry: 'web/index.html', outDir: 'web/dist', publicAssets: []}]});
  await viteBuild(f, undefined);
  const directory = path.join(f.root, 'web/dist');
  assert.equal(fs.existsSync(path.join(directory, 'images/unused.png')), true);
  assert.equal(profiles.verifyDirectory(directory).targetManifest, false);
  assert.equal(fs.existsSync(path.join(directory, targets.manifestName)), false);
});

test('configuration resolves fixed isolated outputs, validates all inputs and has a stable identity', t => {
  const f = fixture(t), resolved = targets.resolve(f.root);
  assert.equal(resolved.configured, true);
  assert.equal(resolved.default, 'main');
  assert.deepEqual(resolved.targets.map(value => value.outDir), ['web/dist/main', 'web/dist/pe']);
  assert.match(resolved.configurationSha256, /^[a-f0-9]{64}$/);
  assert.equal(targets.resolve(f.root).configurationSha256, resolved.configurationSha256);
  f.config.default = 'pe'; f.configure(f.config);
  assert.notEqual(targets.resolve(f.root).configurationSha256, resolved.configurationSha256);
  assert.throws(() => targets.selection(f.root, 'unknown'), /E_WEB_TARGET_UNKNOWN/);
});

for (const [label, mutate] of [
  ['unknown config fields', config => { config.extra = true; }],
  ['empty targets', config => { config.targets = []; }],
  ['missing default', config => { config.default = 'unknown'; }],
  ['duplicate target', config => { config.targets[1].name = 'main'; }],
  ['duplicate entry', config => { config.targets[1].entry = config.targets[0].entry; }],
  ['output override', config => { config.targets[0].outDir = 'somewhere'; }],
  ['upper-case target', config => { config.targets[0].name = 'MAIN'; }],
  ['path-like target', config => { config.targets[0].name = '../main'; }],
  ['reserved device name', config => { config.targets[0].name = 'con'; }],
  ['entry outside web', config => { config.targets[0].entry = 'other/index.html'; }],
  ['non-index entry', config => { config.targets[0].entry = 'web/main/page.html'; }],
  ['output as entry', config => { config.targets[0].entry = 'web/dist/main/index.html'; }],
  ['public as entry', config => { config.targets[0].entry = 'web/public/index.html'; }],
  ['entry traversal', config => { config.targets[0].entry = 'web/main/../../index.html'; }],
  ['missing entry', config => { config.targets[0].entry = 'web/missing/index.html'; }],
  ['asset traversal', config => { config.targets[0].publicAssets = ['../src/main.ts']; }],
  ['absolute asset', config => { config.targets[0].publicAssets = ['/absolute']; }],
  ['backslash asset', config => { config.targets[0].publicAssets = ['images\\logo.png']; }],
  ['missing asset', config => { config.targets[0].publicAssets = ['images/missing.png']; }],
  ['directory asset', config => { config.targets[0].publicAssets = ['images']; }],
  ['duplicate asset', config => { config.targets[0].publicAssets = ['images/logo.png', 'IMAGES/LOGO.PNG']; }],
  ['overlapping asset', config => { config.targets[0].publicAssets = ['images/logo.png', 'images/logo.png/file']; }],
  ['reserved HTML asset', config => { config.targets[0].publicAssets = ['index.html']; }],
  ['reserved metadata asset', config => { config.targets[0].publicAssets = [targets.manifestName]; }],
  ['reserved legal asset', config => { config.targets[0].publicAssets = ['legal/notice.txt']; }],
]) test('target configuration rejects ' + label, t => {
  const f = fixture(t); mutate(f.config); f.configure(f.config);
  assert.throws(() => targets.resolve(f.root));
});

test('real Vite builds isolate entries and only publish explicitly declared dynamic assets', async t => {
  const f = fixture(t);
  await viteBuild(f, 'main'); await viteBuild(f, 'pe');
  const main = path.join(f.root, 'web/dist/main'), pe = path.join(f.root, 'web/dist/pe');
  for (const [directory, name] of [[main, 'main'], [pe, 'pe']]) {
    const selected = targets.selection(f.root, name);
    assert.equal(profiles.verifyDirectory(directory).webTarget, name);
    assert.equal(targets.verifyTargetDirectory(directory, selected).targetManifest, true);
    assert.equal(fs.existsSync(path.join(directory, 'images/logo.png')), true);
    assert.equal(fs.existsSync(path.join(directory, 'images/unused.png')), false);
    assert.throws(() => targets.verifyTargetDirectory(directory, name === 'main' ? 'pe' : 'main'), /E_WEB_TARGET_MISMATCH/);
    const manifest = identity.readJson(path.join(directory, targets.manifestName));
    assert.ok(manifest.files.some(file => file.path === '.soui-web-profile.json'));
    assert.ok(manifest.files.some(file => file.path === 'index.html'));
    assert.ok(!manifest.files.some(file => file.path === targets.manifestName));
    const scripts = identity.walk(directory).filter(file => file.endsWith('.js')).map(file => fs.readFileSync(path.join(directory, file), 'utf8')).join('\n');
    assert.ok(scripts.includes(name === 'main' ? 'MAIN_ONLY' : 'PE_ONLY'));
    assert.ok(!scripts.includes(name === 'main' ? 'PE_ONLY' : 'MAIN_ONLY'));
  }
  assert.equal(fs.existsSync(path.join(main, 'images/account-key.png')), false);
  assert.equal(fs.existsSync(path.join(pe, 'images/account-key.png')), true);
  f.put('web/dist/main/legal/notice.txt', 'SDK-added legal material');
  assert.equal(profiles.verifyDirectory(main, undefined, {excludeLegal: true}).webTarget, 'main');
  assert.throws(() => profiles.verifyDirectory(main), /E_WEB_TARGET_FILE_IDENTITY/);
});

test('nested target entries preserve diagnostic source projection relative to web/src', async t => {
  const f = fixture(t);
  await viteBuild(f, 'main', 'source-diagnostic');
  const checked = profiles.verifyDirectory(path.join(f.root, 'web/dist/main'), 'source-diagnostic');
  assert.equal(checked.webTarget, 'main');
  assert.ok(checked.typedBusinessSources > 0);
});

for (const changed of ['index.html', 'images/logo.png', 'new.txt', 'missing.png']) {
  test('target manifests reject complete-output drift: ' + changed, async t => {
    const f = fixture(t); await viteBuild(f, 'main');
    const directory = path.join(f.root, 'web/dist/main');
    if (changed === 'missing.png') fs.unlinkSync(path.join(directory, 'images/logo.png'));
    else f.put('web/dist/main/' + changed, 'unexpected bytes');
    assert.throws(() => profiles.verifyDirectory(directory), /E_WEB_TARGET_(?:FILE_IDENTITY|OUTPUT)/);
  });
}

test('expected configuration identity refuses a stale or missing target manifest', async t => {
  const f = fixture(t); await viteBuild(f, 'main');
  const directory = path.join(f.root, 'web/dist/main');
  f.config.targets[0].publicAssets.push('images/account-key.png'); f.configure(f.config);
  assert.throws(() => targets.verifyTargetDirectory(directory, targets.selection(f.root, 'main')), /E_WEB_TARGET_MISMATCH/);
  fs.unlinkSync(path.join(directory, targets.manifestName));
  assert.throws(() => targets.verifyTargetDirectory(directory, targets.selection(f.root, 'main')), /E_WEB_TARGET_MANIFEST_REQUIRED/);
});

test('public assets cannot replace files generated by Vite', async t => {
  const f = fixture(t);
  f.put('web/public/assets', 'would replace generated assets directory');
  f.config.targets[0].publicAssets = ['assets']; f.configure(f.config);
  await assert.rejects(() => viteBuild(f, 'main'), /E_WEB_TARGET_ASSET_COLLISION/);
});

test('CLI resolves default output and rejects unknown commands', t => {
  const f = fixture(t), cli = path.join(sdkRoot, 'tools/internal/web-targets.cjs');
  const good = spawnSync(process.execPath, [cli, 'output', f.root], {encoding: 'utf8'});
  assert.equal(good.status, 0, good.stderr);
  assert.equal(good.stdout.trim(), path.join(f.root, 'web/dist/main'));
  const bad = spawnSync(process.execPath, [cli, 'unknown', f.root], {encoding: 'utf8'});
  assert.notEqual(bad.status, 0);
});

test('unconfigured buildAll runs the existing npm build exactly once without target overrides', t => {
  const f = fixture(t);
  f.put('project.json', JSON.stringify({schema: 'soui.application/v1'}));
  f.put('web/package.json', JSON.stringify({private: true, scripts: {build: 'node build.cjs'}}));
  f.put('web/build.cjs', `const fs = require('node:fs');
if (process.env.SOUI_WEB_TARGET || process.env.SOUI_WEB_PROJECT_ROOT) throw new Error('Unexpected target override');
const file = 'runs.txt'; fs.writeFileSync(file, String(Number(fs.existsSync(file) ? fs.readFileSync(file, 'utf8') : 0) + 1));
`);
  assert.equal(targets.buildAll(f.root).configured, false);
  assert.equal(fs.readFileSync(path.join(f.root, 'web/runs.txt'), 'utf8'), '1');
});

test('configured builds reject a successful npm script that does not publish target evidence', t => {
  const f = fixture(t);
  f.put('web/package.json', JSON.stringify({private: true, scripts: {build: 'node -e "process.exit(0)"'}}));
  assert.throws(() => targets.buildAll(f.root), /E_WEB_TARGET_MANIFEST_REQUIRED/);
});

for (const buildProfile of ['distribution', 'source-diagnostic'])
test('the original npm compatibility pipeline builds every target and reseals classic HTML: ' + buildProfile, async t => {
  const f = fixture(t);
  const profileModule = path.join(sdkRoot, 'tools/internal/web-build-profile.cjs');
  const writer = path.join(sdkRoot, 'tools/internal/create-miniblink-entry.mjs');
  const viteBin = path.join(webModules, 'vite/bin/vite.js');
  const legacyModule = createRequire(path.join(webModules, '../package.json')).resolve('@vitejs/plugin-legacy');
  if (buildProfile === 'source-diagnostic') {
    // Diagnostic validation binds legacy-polyfill labels to the application's
    // installed package bytes. This fixture uses the pinned existing packages.
    for (const name of ['core-js', 'systemjs']) {
      const destination = identity.safePath(f.root, 'web/node_modules/' + name);
      fs.cpSync(identity.safePath(webModules, name), destination, {recursive: true});
    }
  }
  f.put('web/package.json', JSON.stringify({private: true, type: 'module', scripts: {build:
    'node "' + viteBin + '" build && node "' + writer + '" dist TargetFixture'}}));
  f.put('web/vite.config.mjs', `import legacy from ${JSON.stringify(pathToFileURL(legacyModule).href)};
import profiles from ${JSON.stringify(pathToFileURL(profileModule).href)};
const profile = profiles.buildProfile();
export default {base:'./',plugins:[legacy({targets:['Chrome 49']}),profile.plugin],build:{manifest:true,...profile.build}};
`);
  const result = await withEnvironment({SOUI_WEB_BUILD_PROFILE: buildProfile}, () => targets.buildAll(f.root));
  assert.equal(result.targets.length, 2);
  for (const selected of result.targets) {
    const directory = path.join(f.root, selected.outDir);
    assert.match(fs.readFileSync(path.join(directory, 'index.html'), 'utf8'), /System.import/);
    const checked = profiles.verifyDirectory(directory, buildProfile);
    assert.equal(checked.webTarget, selected.name);
    if (buildProfile === 'source-diagnostic') assert.ok(checked.typedBusinessSources > 0);
  }
  if (buildProfile === 'distribution') {
    const env = {...process.env}; delete env.SOUI_WEB_TARGET; delete env.SOUI_WEB_PROJECT_ROOT;
    env.SOUI_WEB_BUILD_PROFILE = buildProfile;
    const direct = process.platform === 'win32'
      ? spawnSync(process.env.ComSpec || 'cmd.exe', ['/d', '/s', '/c', 'npm run build'],
        {cwd: path.join(f.root, 'web'), env, encoding: 'utf8'})
      : spawnSync('npm', ['run', 'build'], {cwd: path.join(f.root, 'web'), env, encoding: 'utf8'});
    assert.equal(direct.status, 0, direct.stdout + direct.stderr);
    assert.equal(profiles.verifyDirectory(path.join(f.root, 'web/dist/main')).webTarget, 'main');
  }
});
