'use strict';

// These filesystem contract tests never compile or execute the synthetic native
// payloads. Inline maps exercise publication/identity, not debugger breakpoints.
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const vm = require('node:vm');
const identity = require('../../tools/internal/delivery-identity.cjs');
const profiles = require('../../tools/internal/web-build-profile.cjs');
const evidence = require('../../tools/internal/build-evidence.cjs');
const deployment = require('../../tools/internal/deploy-resources.cjs');

const diagnostic = 'source-diagnostic';
const distribution = 'distribution';
const target = 'ProfileApp';
const version = '0.5.0-preview.1';
const fixturePrefix = 'soui-web-profile-integration-';

function removeOwned(directory, owner) {
  const absolute = path.resolve(directory);
  const root = path.resolve(owner);
  const relative = path.relative(root, absolute);
  assert.ok(relative === '' || (!path.isAbsolute(relative) && relative !== '..' &&
    !relative.startsWith('..' + path.sep)), 'Recursive cleanup must stay inside its created fixture');
  assert.equal(identity.safePath(absolute), absolute, 'Cleanup must not follow linked paths');
  fs.rmSync(absolute, {recursive: true, force: true});
}

function fixture(t) {
  const temporary = fs.realpathSync(os.tmpdir());
  const root = fs.realpathSync(fs.mkdtempSync(path.join(temporary, fixturePrefix)));
  t.after(() => {
    assert.equal(path.dirname(root), temporary, 'Only this directly created temporary directory is owned');
    assert.ok(path.basename(root).startsWith(fixturePrefix));
    if (fs.existsSync(root)) assert.equal(fs.realpathSync(root), root);
    removeOwned(root, root);
  });
  const put = (name, data) => {
    const file = identity.safePath(root, name);
    fs.mkdirSync(path.dirname(file), {recursive: true});
    fs.writeFileSync(file, data);
    return file;
  };
  put('VERSION', version + '\n');
  put('sdk/include/soui/version.h', `#define SOUI_SDK_VERSION_STRING "${version}"\n`);
  put('sdk/web/package.json', JSON.stringify({version}));
  put('sdk/web/package-lock.json', JSON.stringify({version, packages: {'': {version}}}));
  put('sdk/web/src/index.ts', 'export const sdkFixture = true;\n');
  put('sdk/web/dist/index.js', 'export const sdkFixture = true;\n');
  put('sdk/web/dist/index.d.ts', 'export declare const sdkFixture: boolean;\n');
  put('project.json', JSON.stringify({schema: 'soui.application/v1', nativeTarget: target,
    runtimeProfile: 'compatibility'}));
  put('web/src/main.tsx', 'export const diagnosticFixture = <main>Fixture</main>;\n');
  put('web/package.json', JSON.stringify({private: true}));
  put('THIRD_PARTY_NOTICES.md', '# Synthetic SDK notices\n');
  put('third_party/licenses/fixture-LICENSE.txt', 'Synthetic licensed fixture bytes.\n');
  put(`out/x64/bin/Release/${target}/${target}.exe`, Buffer.from('SYNTHETIC EXE: never executable\0'));
  put(`out/x64/bin/Release/${target}/node.dll`, Buffer.from('SYNTHETIC DLL: never loaded\0'));
  put('out/sdk-x64/Release/soui_sdk.lib', Buffer.from('SYNTHETIC SDK archive\0'));
  return {root, put, webRoot: path.join(root, 'web'), dist: path.join(root, 'web/dist'),
    app: path.join(root, `out/x64/bin/Release/${target}`),
    buildFile: path.join(root, 'out/x64/build-Release.json'),
    webFile: path.join(root, 'out/web-applications.json')};
}

function publishWeb(f, profile, marked = true) {
  removeOwned(f.dist, f.root);
  f.put('web/dist/index.html', '<!doctype html><script src="./assets/app.js"></script>\n');
  let script = 'window.fixtureReady = true;\n';
  if (profile === diagnostic) {
    const map = {version: 3, file: 'app.js', sources: ['../../src/main.tsx'],
      sourcesContent: [fs.readFileSync(path.join(f.webRoot, 'src/main.tsx'), 'utf8')],
      names: [], mappings: 'AAAA'};
    script += '//# sourceMappingURL=data:application/json;base64,' +
      Buffer.from(JSON.stringify(map)).toString('base64') + '\n';
  }
  f.put('web/dist/assets/app.js', script);
  if (marked) {
    const {plugin} = profiles.buildProfile(profile);
    plugin.configResolved({root: f.webRoot, build: {outDir: 'dist'}});
    const checked = plugin.closeBundle();
    assert.equal(checked.profile, profile);
    assert.equal(checked.marked, true);
  }
}

function beginAndRecord(f, profile) {
  const selected = {webProfile: profile};
  evidence.start(f.root, 'x64', 'Release', false, selected);
  const checked = evidence.webEvidence(f.root, 'x64', 'Release', false, true, selected);
  assert.equal(checked.webProfile, profile);
  return selected;
}

function deployAndFinish(f, selected) {
  const deployed = evidence.deployWeb(f.root, 'x64', 'Release', false, selected);
  assert.equal(deployed.webProfile, selected.webProfile);
  assert.deepEqual(deployed.targets, [target]);
  evidence.finish(f.root, 'x64', 'Release', false, selected);
  return evidence.verifyBuild(f.root, 'x64', 'Release', false, selected);
}

function resourceSnapshot(f) {
  const directory = path.join(f.app, 'resources');
  return identity.snapshot(directory, identity.walk(directory));
}

test('diagnostic profile binds actual marker, Web evidence, deployment and native payload receipt', t => {
  const f = fixture(t);
  publishWeb(f, diagnostic);
  const selected = beginAndRecord(f, diagnostic);
  assert.equal(evidence.webEvidence(f.root, 'x64', 'Release', false, false, selected).webVerified, true);
  const build = deployAndFinish(f, selected);
  assert.equal(build.webProfile, diagnostic);
  assert.equal(build.webBuild.profile, diagnostic);
  assert.equal(build.nativeBuild, 'passed');
  assert.equal(build.releaseReady, false);
  const web = identity.readJson(f.webFile);
  assert.equal(web.schema, 'soui.web-build/v2');
  assert.equal(web.webProfile, diagnostic);
  assert.equal(web.applications[0].marked, true);
  assert.ok(web.artifacts.files.some(file => file.path === 'sdk/web/dist/index.d.ts'));
  const deployed = profiles.verifyDirectory(path.join(f.app, 'resources'), diagnostic, {excludeLegal: true});
  assert.equal(deployed.typedBusinessSources, 1);
  assert.equal(deployed.maps, 1);
  assert.ok(build.payloads[target].files.some(file => file.path === 'resources/' + profiles.manifestName));
  assert.equal(fs.readFileSync(path.join(f.app, 'resources/legal/THIRD_PARTY_NOTICES.md'), 'utf8'),
    fs.readFileSync(path.join(f.root, 'THIRD_PARTY_NOTICES.md'), 'utf8'));
  const accepted = evidence.verifyAcceptedBuild(f.root, 'x64', 'Release');
  assert.equal(accepted.build.buildId, build.buildId);
  assert.equal(accepted.selected.webProfile, diagnostic);
  assert.equal(evidence.verifyAcceptedBuild(f.root, 'x64', 'Release', false,
    {nativeToolchain: 'MSVC143'}).selected.webProfile, diagnostic);
});

test('explicit cross-profile SkipWeb, finish and accepted readers refuse diagnostic reuse', t => {
  const f = fixture(t);
  publishWeb(f, diagnostic);
  const selected = beginAndRecord(f, diagnostic);
  assert.throws(() => evidence.webEvidence(f.root, 'x64', 'Release', false, false,
    {webProfile: distribution}), /E_WEB_PROFILE_MISMATCH/);
  assert.throws(() => evidence.finish(f.root, 'x64', 'Release', false,
    {webProfile: distribution}), /Web build profile changed/);
  assert.equal(identity.readJson(f.buildFile).nativeBuild, 'pending');
  deployAndFinish(f, selected);
  for (const reader of [evidence.verifyBuild, evidence.verifyAcceptedBuild]) {
    assert.throws(() => reader(f.root, 'x64', 'Release', false,
      {webProfile: distribution}), /Web profile differs/);
  }
});

test('legacy web-build/v1 evidence cannot authorize SkipWeb, finish or deployment', t => {
  const f = fixture(t);
  publishWeb(f, distribution);
  const selected = beginAndRecord(f, distribution);
  evidence.deployWeb(f.root, 'x64', 'Release', false, selected);
  const before = resourceSnapshot(f);
  const current = identity.readJson(f.webFile);
  fs.writeFileSync(f.webFile, identity.encoded({schema: 'soui.web-build/v1',
    sourceSha256: current.sourceSha256, artifacts: current.artifacts}));
  assert.throws(() => evidence.webEvidence(f.root, 'x64', 'Release', false, false, selected), /SkipWeb requires/);
  assert.throws(() => evidence.finish(f.root, 'x64', 'Release', false, selected), /SkipWeb requires/);
  assert.throws(() => evidence.deployWeb(f.root, 'x64', 'Release', false, selected), /SkipWeb requires/);
  assert.deepEqual(resourceSnapshot(f), before);
  assert.equal(identity.readJson(f.buildFile).nativeBuild, 'pending');
});

for (const change of ['modified-script', 'standalone-source']) {
  test(`recorded ${change} refuses finish/deployment and preserves existing resources`, t => {
    const f = fixture(t);
    publishWeb(f, diagnostic);
    const selected = beginAndRecord(f, diagnostic);
    evidence.deployWeb(f.root, 'x64', 'Release', false, selected);
    const before = resourceSnapshot(f);
    if (change === 'modified-script') {
      const script = path.join(f.dist, 'assets/app.js');
      // Keep the map annotation valid and terminal so the refusal specifically
      // tests the recorded script identity rather than malformed map syntax.
      fs.writeFileSync(script, '// Changed after publication\n' + fs.readFileSync(script, 'utf8'));
    } else f.put('web/dist/leaked-source.tsx', 'export const leaked = <main/>;\n');
    const reason = change === 'modified-script' ? /E_WEB_SCRIPT_IDENTITY/ : /E_WEB_STANDALONE_SOURCE/;
    assert.throws(() => evidence.finish(f.root, 'x64', 'Release', false, selected), reason);
    assert.throws(() => evidence.deployWeb(f.root, 'x64', 'Release', false, selected), reason);
    assert.throws(() => deployment.replace(f.dist, f.app, f.root, diagnostic), reason);
    assert.deepEqual(resourceSnapshot(f), before);
    assert.equal(identity.readJson(f.buildFile).nativeBuild, 'pending');
    assert.deepEqual(fs.readdirSync(f.app).filter(name => name.startsWith('.resources-')), []);
  });
}

test('SDK-only default artifacts retain declarations and reject a diagnostic application profile', t => {
  const f = fixture(t);
  const selected = {webProfile: diagnostic};
  assert.throws(() => evidence.start(f.root, 'x64', 'Release', true, selected), /require an application Web build/);
  assert.throws(() => evidence.webEvidence(f.root, 'x64', 'Release', true, true, selected), /require an application Web build/);
  evidence.start(f.root, 'x64', 'Release', true);
  evidence.webEvidence(f.root, 'x64', 'Release', true, true);
  evidence.finish(f.root, 'x64', 'Release', true);
  const accepted = evidence.verifyAcceptedBuild(f.root, 'x64', 'Release', true);
  assert.equal(accepted.build.webProfile, distribution);
  assert.equal(accepted.build.releaseReady, false);
  assert.deepEqual(accepted.build.payloads, {});
  const web = identity.readJson(path.join(f.root, 'out/web-sdk.json'));
  assert.deepEqual(web.applications, []);
  assert.ok(web.artifacts.files.some(file => file.path === 'sdk/web/dist/index.d.ts'));
});

test('SDK-only distribution SkipWeb binds verified output until native finish', t => {
  const f = fixture(t);
  const selected = {webProfile: distribution};
  const buildFile = path.join(f.root, 'out/sdk-x64/build-Release.json');
  const webFile = path.join(f.root, 'out/web-sdk.json');
  const library = path.join(f.root, 'out/sdk-x64/Release/soui_sdk.lib');
  const libraryHash = identity.hash(fs.readFileSync(library));

  // Establish genuine Web evidence for the later SkipWeb invocation. The
  // archive is an inert fixture; no compiler, native payload or runtime runs.
  evidence.start(f.root, 'x64', 'Release', true, selected);
  evidence.webEvidence(f.root, 'x64', 'Release', true, true, selected);
  evidence.finish(f.root, 'x64', 'Release', true, selected);
  const priorBuildId = identity.readJson(buildFile).buildId;
  const originalWebEvidence = fs.readFileSync(webFile);

  evidence.start(f.root, 'x64', 'Release', true, selected);
  assert.notEqual(identity.readJson(buildFile).buildId, priorBuildId);
  assert.equal(evidence.webEvidence(f.root, 'x64', 'Release', true, false, selected).webVerified, true);
  const sourceHash = identity.source(f.root).sha256;
  f.put('sdk/web/dist/index.js', 'export const sdkFixture = false; // Changed after SkipWeb verification\n');
  assert.equal(identity.source(f.root).sha256, sourceHash, 'Output drift must not depend on source drift');
  assert.equal(identity.hash(fs.readFileSync(library)), libraryHash, 'Native fixture bytes remain unchanged');

  assert.throws(() => evidence.finish(f.root, 'x64', 'Release', true, selected), /SkipWeb requires/);
  assert.equal(identity.readJson(buildFile).nativeBuild, 'pending');
  assert.deepEqual(fs.readFileSync(webFile), originalWebEvidence, 'Failure preserves the original verified Web receipt');
});

test('old custom distribution output remains supported without a profile marker', t => {
  const f = fixture(t);
  publishWeb(f, distribution, false);
  const selected = beginAndRecord(f, distribution);
  deployAndFinish(f, selected);
  const application = identity.readJson(f.webFile).applications[0];
  assert.equal(application.marked, false);
  assert.equal(application.manifestSha256, null);
  assert.equal(evidence.verifyAcceptedBuild(f.root, 'x64', 'Release').build.webProfile, distribution);
  evidence.start(f.root, 'x64', 'Release', false, {webProfile: diagnostic});
  assert.throws(() => evidence.webEvidence(f.root, 'x64', 'Release', false, true,
    {webProfile: diagnostic}), /E_WEB_MANIFEST_REQUIRED/);
});

for (const reserved of ['legal', 'LEGAL', 'LeGaL']) {
  test(`Web-owned ${reserved} path is refused before replacing deployed resources`, t => {
    const f = fixture(t);
    publishWeb(f, distribution);
    const selected = beginAndRecord(f, distribution);
    evidence.deployWeb(f.root, 'x64', 'Release', false, selected);
    const before = resourceSnapshot(f);
    f.put(`web/dist/${reserved}/injected.txt`, 'Web output cannot own SDK legal material.\n');
    assert.throws(() => deployment.replace(f.dist, f.app, f.root, distribution), /reserved legal path/);
    assert.deepEqual(resourceSnapshot(f), before);
    assert.deepEqual(fs.readdirSync(f.app).filter(name => name.startsWith('.resources-')), []);
  });
}

test('switching Web profiles redeploys resources while native bytes remain unchanged', t => {
  const f = fixture(t);
  const executable = identity.hash(fs.readFileSync(path.join(f.app, target + '.exe')));
  const kernel = identity.hash(fs.readFileSync(path.join(f.app, 'node.dll')));
  let previous;
  for (const profile of [distribution, diagnostic, distribution]) {
    publishWeb(f, profile);
    const selected = beginAndRecord(f, profile);
    const build = deployAndFinish(f, selected);
    const current = resourceSnapshot(f);
    if (previous) assert.notEqual(current.sha256, previous.sha256);
    assert.equal(build.webProfile, profile);
    assert.equal(identity.hash(fs.readFileSync(path.join(f.app, target + '.exe'))), executable);
    assert.equal(identity.hash(fs.readFileSync(path.join(f.app, 'node.dll'))), kernel);
    assert.equal(fs.readFileSync(path.join(f.app, 'resources/assets/app.js'), 'utf8')
      .includes('sourceMappingURL'), profile === diagnostic);
    assert.equal(profiles.verifyDirectory(path.join(f.app, 'resources'), profile,
      {excludeLegal: true}).profile, profile);
    previous = current;
  }
});

test('partial selections retain both previously selected native and diagnostic Web fields', () => {
  // Isolate only the real selector's argument forwarding. Its observer always
  // rejects, so no synthetic LLVM acceptance or compiler evidence is produced.
  const sourceFile = path.resolve(__dirname, '../../tools/internal/build-evidence.cjs');
  const source = fs.readFileSync(sourceFile, 'utf8');
  const start = source.indexOf('function verifyAcceptedBuild(');
  const end = source.indexOf('function beginSmoke(', start);
  assert.ok(start >= 0 && end > start, 'The actual selection function must remain identifiable');
  const root = path.resolve(os.tmpdir(), fixturePrefix + 'selection-only');
  const directory = path.join(root, 'out/x64');
  const request = path.join(directory, 'llvm-profile-request-Release.json');
  const current = {webProfile: diagnostic,
    nativeToolchain: {profile: 'LLVM22MSVC142', requestFile: {path: request}}};
  const calls = [];
  const rejected = new Error('E_CONTROLLED_OBSERVER_REJECTED');
  const context = {
    path, webProfiles: profiles,
    id: {readJson(file) { assert.equal(file, path.join(directory, 'build-Release.json')); return current; }},
    output(...args) { assert.deepEqual(args, [root, 'x64', 'Release', false]); return directory; },
    verifyBuild(...args) { calls.push({reader: 'build', selected: {...args[4]}}); return current; },
    require(name) {
      assert.equal(name, './llvm-build-observer.cjs');
      return {verifyStandardBuild(...args) {
        calls.push({reader: 'observer', selected: {...args[4]}}); throw rejected;
      }};
    },
  };
  const verify = vm.runInNewContext(source.slice(start, end) + '\nverifyAcceptedBuild;', context,
    {filename: sourceFile});
  for (const selected of [{webProfile: diagnostic}, {nativeToolchain: 'LLVM22MSVC142', profileRequest: request}]) {
    assert.throws(() => verify(root, 'x64', 'Release', false, selected), error => error === rejected);
    assert.deepEqual(calls.map(call => call.reader), ['build', 'observer']);
    for (const call of calls.splice(0)) {
      assert.deepEqual(call.selected, {nativeToolchain: 'LLVM22MSVC142', profileRequest: request, webProfile: diagnostic});
    }
  }
});
