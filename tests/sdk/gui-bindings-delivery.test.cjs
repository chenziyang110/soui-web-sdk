'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const cp = require('node:child_process');
const gui = require('../../tools/sdk/gui-bindings-delivery.cjs');
const sdk = require('../../tools/sdk/soui-sdk.cjs');
const identity = require('../../tools/internal/delivery-identity.cjs');
const version = '0.5.0-preview.1';
const previousPeerRange = '>=0.4.0-0 <0.5.0 || >=0.5.0-0 <0.6.0';
const currentPeerRange = previousPeerRange + ' || 0.6.0-preview.3';
const repository = path.resolve(__dirname, '../..');
test('repository GUI metadata accepts the actual VERSION and lock files', () => {
  const current = fs.readFileSync(path.join(repository, 'VERSION'), 'utf8').trim();
  assert.equal(gui.metadata(repository), current);
});

function fixture(t, targetVersion = version, nativePeerRange = currentPeerRange) {
  const version = targetVersion;
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-optional-gui-'));
  t.after(() => {
    assert.ok(fs.realpathSync(root).startsWith(fs.realpathSync(os.tmpdir()) + path.sep));
    fs.rmSync(root, {recursive: true, force: true});
  });
  const source = path.join(root, 'source');
  const write = (name, data) => { fs.mkdirSync(path.dirname(path.join(source, name)), {recursive: true}); fs.writeFileSync(path.join(source, name), data); };
  for (const name of sdk.managedRoots) write(`${name}/fixture.txt`, 'fixture\n');
  for (const name of sdk.managedFiles) write(name, 'fixture\n');
  write('VERSION', version + '\n');
  write('tools/sdk/gui-bindings-delivery.cjs', fs.readFileSync(path.join(repository, 'tools/sdk/gui-bindings-delivery.cjs')));
  write('tools/internal/sdk-version.cjs', fs.readFileSync(path.join(repository, 'tools/internal/sdk-version.cjs')));
  write('sdk/bindings/shared/controller.ts', 'export const shared = true;\n');
  for (const entry of gui.packages) {
    const meta = {name: entry.name, version, type: 'module', main: `./${entry.import}`, types: `./${entry.types}`,
      exports: {'.': {import: `./${entry.import}`, types: `./${entry.types}`}}};
    const lock = {version, packages: {'': {version}}};
    if (entry.framework) {
      meta.peerDependencies = {'@soui/native-sdk': nativePeerRange, [entry.framework]: '*'};
      meta.devDependencies = {'@soui/native-sdk': 'file:../../web'};
      lock.packages['node_modules/@soui/native-sdk'] = {version};
    }
    write(`${entry.root}/package.json`, identity.encoded(meta));
    write(`${entry.root}/package-lock.json`, identity.encoded(lock));
    write(`${entry.root}/tsconfig.json`, '{}\n');
    write(`${entry.root}/src/index.ts`, 'export const source = true;\n');
    write(`${entry.root}/${entry.import}`, 'export const compiled = true;\n');
    write(`${entry.root}/${entry.types}`, 'export declare const compiled: true;\n');
    if (entry.framework) {
      write(`${entry.root}/dist/shared/controller.js`, 'export const shared = true;\n');
      write(`${entry.root}/dist/shared/controller.d.ts`, 'export declare const shared: true;\n');
    }
  }
  const record = (recordRoot = source) => {
    const writeRecord = (name, data) => {
      const file = path.join(recordRoot, name);
      fs.mkdirSync(path.dirname(file), {recursive: true}); fs.writeFileSync(file, data);
    };
    const evidenceDirectory = 'out/gui-bindings/build-00000000-0000-0000-0000-000000000000';
    const commands = gui.packages.flatMap(entry => [['ci', '--ignore-scripts', '--no-audit', '--no-fund', '--install-links'], ['run', 'build']]
      .map((args, index) => {
        const stdout = `${entry.name.replaceAll('/', '-')}-${index}.stdout.log`, stderr = `${entry.name.replaceAll('/', '-')}-${index}.stderr.log`;
        writeRecord(`${evidenceDirectory}/${stdout}`, 'fixture command\n'); writeRecord(`${evidenceDirectory}/${stderr}`, '');
        return {cwd: entry.root, executable: process.execPath, args: ['fixture-npm-cli', ...args], exitCode: 0, error: null, stdout, stderr,
          logs: {stdout: {bytes: 16, sha256: identity.hash(Buffer.from('fixture command\n'))}, stderr: {bytes: 0, sha256: identity.hash(Buffer.alloc(0))}}};
      }));
    writeRecord(gui.reportName, identity.encoded({schema: 'soui.gui-bindings-build/v1', version,
      source: gui.source(recordRoot), output: gui.outputs(recordRoot), evidenceDirectory, commands}));
  };
  record();
  return {root, source, write, record};
}
function legacyToolBytes() {
  // The real previous producer is data only: never require or execute it.
  const bytes = identity.normalize(fs.readFileSync(path.join(__dirname,
    'fixtures/gui-bindings-delivery-preview-legacy.cjs.txt')), 'lf');
  assert.equal(bytes.length, 13693);
  assert.equal(identity.hash(bytes), '26305269c19891cd5dfb6d781985114f7b6aa1d6437d22213d4cb6ba25c10ec3');
  return bytes;
}
function historicalPackage(t, targetVersion = version, tool = legacyToolBytes()) {
  const f = fixture(t, targetVersion, previousPeerRange), packed = path.join(f.root, 'historical-package');
  f.write('tools/sdk/gui-bindings-delivery.cjs', tool);
  fs.unlinkSync(path.join(f.source, 'tools/internal/sdk-version.cjs'));
  // Construct the previous declaration as data using its full source set.
  const names = ['sdk/web/src', 'sdk/bindings/shared', 'sdk/bindings/react/src', 'sdk/bindings/vue/src']
    .flatMap(name => identity.walk(f.source, name, (_, leaf) => identity.skipped.has(leaf)));
  for (const entry of gui.packages) for (const file of ['package.json', 'package-lock.json', 'tsconfig.json']) names.push(`${entry.root}/${file}`);
  names.push('tools/sdk/gui-bindings-delivery.cjs');
  const input = identity.snapshot(f.source, names, true), output = gui.outputs(f.source);
  // The source-only packer copies bytes and does not execute the saved tool.
  sdk.pack(f.source, packed, targetVersion);
  const manifestFile = path.join(packed, 'sdk-manifest.json');
  const manifest = JSON.parse(fs.readFileSync(manifestFile));
  manifest.optionalGuiBindings = gui.declaration('', targetVersion, input, output);
  for (const file of output.files) {
    const target = path.join(packed, 'payload', file.path);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.copyFileSync(path.join(f.source, file.path), target);
    manifest.files.push({...file, sourceNormalization: 'none'});
  }
  manifest.files.sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
  fs.writeFileSync(manifestFile, identity.encoded(manifest));
  return {...f, packed, manifest};
}
test('previous preview optional GUI reads, installs and downgrades with its fixed declaration', t => {
  const old = historicalPackage(t), project = path.join(old.root, 'project');
  const read = sdk.readPackage(old.packed);
  assert.equal(JSON.parse(fs.readFileSync(path.join(old.packed, 'payload/sdk/bindings/react/package.json')))
    .peerDependencies['@soui/native-sdk'], previousPeerRange);
  assert.equal(read.optionalGuiBindings.source.files.some(file => file.path === 'tools/internal/sdk-version.cjs'), false);
  fs.mkdirSync(path.join(project, 'app'), {recursive: true});
  fs.writeFileSync(path.join(project, 'app/business.txt'), 'business-owned\n');
  sdk.install(old.packed, project);
  assert.deepEqual(gui.verify(project), read.optionalGuiBindings);
  const next = fixture(t, '0.5.0'), packed = path.join(next.root, 'stable');
  sdk.pack(next.source, packed, '0.5.0', {withGuiBindings: true});
  sdk.install(packed, project);
  assert.equal(gui.verify(project).sdkVersion, '0.5.0');
  assert.equal(sdk.readLock(project).manifest.optionalGuiBindings.source.files.some(file => file.path === 'tools/internal/sdk-version.cjs'), true);
  sdk.install(old.packed, project);
  assert.equal(sdk.readLock(project).manifest.version, version);
  assert.deepEqual(gui.verify(project), read.optionalGuiBindings);
  assert.equal(fs.existsSync(path.join(project, 'tools/internal/sdk-version.cjs')), false);
  assert.equal(fs.readFileSync(path.join(project, 'app/business.txt'), 'utf8'), 'business-owned\n');
});
test('current candidate delivers the exact new peer and rejects the previous declaration', t => {
  const candidate = '0.6.0-preview.3', f = fixture(t, candidate);
  const packed = path.join(f.root, 'candidate'), installed = path.join(f.root, 'installed');
  sdk.pack(f.source, packed, candidate, {withGuiBindings: true});
  sdk.install(packed, installed);
  assert.equal(gui.verify(installed).sdkVersion, candidate);
  for (const framework of ['react', 'vue']) {
    const name = `sdk/bindings/${framework}/package.json`;
    const delivered = JSON.parse(fs.readFileSync(path.join(installed, name)));
    assert.equal(delivered.peerDependencies['@soui/native-sdk'], currentPeerRange);
  }
  const name = 'sdk/bindings/react/package.json';
  const metadata = JSON.parse(fs.readFileSync(path.join(f.source, name)));
  metadata.peerDependencies['@soui/native-sdk'] = previousPeerRange;
  f.write(name, identity.encoded(metadata));
  assert.throws(() => gui.metadata(f.source), /borrow the exact local SDK/);
});
test('GUI peer declarations admit only identities covered by their range', t => {
  for (const peer of [previousPeerRange, currentPeerRange]) {
    for (const target of ['0.4.0-preview.1', '0.4.0-rc.1', '0.4.2', '0.5.0-preview.1', '0.5.0-rc.1', '0.5.3']) {
      const f = fixture(t, target, peer);
      assert.equal(gui.metadata(f.source), target);
    }
    for (const target of ['0.4.1-preview.1', '0.5.1-preview.1', '0.6.0-preview.2', '0.6.0']) {
      const f = fixture(t, target, peer);
      assert.throws(() => gui.metadata(f.source), /borrow the exact local SDK/);
    }
  }
  const current = fixture(t, '0.6.0-preview.3', currentPeerRange);
  assert.equal(gui.metadata(current.source), '0.6.0-preview.3');
  const prior = fixture(t, '0.6.0-preview.3', previousPeerRange);
  assert.throws(() => gui.metadata(prior.source), /borrow the exact local SDK/);
});
test('historical optional GUI verifies the complete source and output sets', t => {
  for (const mutate of [
    root => fs.writeFileSync(path.join(root, 'sdk/web/src/undeclared.ts'), 'extra source\n'),
    root => fs.writeFileSync(path.join(root, 'sdk/web/dist/index.js'), 'changed output\n'),
    root => fs.writeFileSync(path.join(root, 'sdk/web/dist/undeclared.js'), 'extra output\n')
  ]) {
    const old = historicalPackage(t);
    assert.equal(sdk.readPackage(old.packed).version, version);
    mutate(path.join(old.packed, 'payload'));
    assert.throws(() => sdk.readPackage(old.packed), /modified|file set changed/);
  }
});
test('an installed historical lock does not hide a failed or changed GUI build record', t => {
  const old = historicalPackage(t), project = path.join(old.root, 'installed');
  sdk.install(old.packed, project);
  fs.cpSync(path.join(old.source, 'out/gui-bindings'), path.join(project, 'out/gui-bindings'), {recursive: true});
  const reportFile = path.join(project, gui.reportName), report = JSON.parse(fs.readFileSync(reportFile));
  report.source = old.manifest.optionalGuiBindings.source;
  report.output = old.manifest.optionalGuiBindings.output;
  fs.writeFileSync(reportFile, identity.encoded(report));
  assert.deepEqual(gui.verify(project), old.manifest.optionalGuiBindings);
  report.commands[0].exitCode = 1;
  fs.writeFileSync(reportFile, identity.encoded(report));
  assert.throws(() => gui.verify(project), /compile command/);
  report.commands[0].exitCode = 0;
  fs.writeFileSync(reportFile, identity.encoded(report));
  fs.writeFileSync(path.join(project, report.evidenceDirectory, report.commands[0].stdout), 'changed log\n');
  assert.throws(() => gui.verify(project), /compile log changed/);
});
test('historical producer cannot authorize RC or stable declarations', t => {
  for (const targetVersion of ['0.5.0-rc.1', '0.5.0']) {
    const old = historicalPackage(t, targetVersion);
    assert.throws(() => sdk.readPackage(old.packed), /Historical GUI source identity/);
  }
});
test('current GUI producer cannot omit the helper in packages or installed locks', t => {
  const current = fs.readFileSync(path.join(repository, 'tools/sdk/gui-bindings-delivery.cjs'));
  for (const targetVersion of [version, '0.5.0-rc.1', '0.5.0']) {
    const invalid = historicalPackage(t, targetVersion, current);
    assert.throws(() => sdk.readPackage(invalid.packed), /Historical GUI/);
    const installed = path.join(invalid.root, 'invalid-installed');
    fs.cpSync(path.join(invalid.packed, 'payload'), installed, {recursive: true});
    fs.writeFileSync(path.join(installed, 'sdk-lock.json'), identity.encoded({schema: 'soui.sdk-lock/v1',
      manifestSha256: identity.hash(identity.encoded(invalid.manifest)), manifest: invalid.manifest}));
    assert.throws(() => gui.verify(installed), /Historical GUI/);
    assert.throws(() => gui.source(invalid.source));
    const output = path.join(invalid.root, 'new-package');
    assert.throws(() => sdk.pack(invalid.source, output, targetVersion, {withGuiBindings: true}));
    assert.equal(fs.existsSync(output), false);
  }
});
test('historical compatibility requires exact previous producer bytes and absent helper', t => {
  const changed = historicalPackage(t, version, Buffer.concat([legacyToolBytes(), Buffer.from('// changed\n')]));
  assert.throws(() => sdk.readPackage(changed.packed), /Historical GUI producer bytes differ/);
  const old = historicalPackage(t);
  const helper = path.join(old.packed, 'payload/tools/internal/sdk-version.cjs');
  fs.writeFileSync(helper, fs.readFileSync(path.join(repository, 'tools/internal/sdk-version.cjs')));
  assert.throws(() => sdk.readPackage(old.packed), /Historical GUI source identity/);
});
test('an installed historical declaration cannot bypass new GUI packaging input requirements', t => {
  const old = historicalPackage(t), project = path.join(old.root, 'installed');
  sdk.install(old.packed, project);
  assert.deepEqual(gui.verify(project), old.manifest.optionalGuiBindings);
  const output = path.join(old.root, 'repacked');
  assert.throws(() => sdk.pack(project, output, version, {withGuiBindings: true}));
  assert.equal(fs.existsSync(output), false);
  // Even an extra helper cannot turn the old source declaration into a new one.
  fs.copyFileSync(path.join(repository, 'tools/internal/sdk-version.cjs'), path.join(project, 'tools/internal/sdk-version.cjs'));
  assert.throws(() => sdk.pack(project, output, version, {withGuiBindings: true}), /Historical GUI source identity/);
  assert.equal(fs.existsSync(output), false);
});
for (const targetVersion of ['0.5.0-rc.1', '0.5.0']) test('optional GUI delivery preserves exact ' + targetVersion + ' identity', t => {
  const f = fixture(t, targetVersion), packed = path.join(f.root, 'packed'), installed = path.join(f.root, 'installed');
  sdk.pack(f.source, packed, targetVersion, {withGuiBindings: true});
  const manifest = sdk.readPackage(packed);
  assert.equal(manifest.version, targetVersion);
  assert.equal(manifest.channel, targetVersion.includes('-rc.') ? 'rc' : 'stable');
  assert.equal(manifest.releaseReady, false);
  assert.equal(manifest.optionalGuiBindings.sdkVersion, targetVersion);
  sdk.install(packed, installed);
  assert.equal(gui.verify(installed).sdkVersion, targetVersion);
  const packageFile = path.join(installed, 'sdk/bindings/react/package.json');
  const metadata = JSON.parse(fs.readFileSync(packageFile));
  fs.writeFileSync(packageFile, identity.encoded({...metadata, version: '0.5.0-preview.1'}));
  assert.throws(() => gui.verify(installed), /differ|modified/);
});

test('default source delivery remains framework-free and does not require compiled GUI output', t => {
  const f = fixture(t);
  fs.rmSync(path.join(f.source, 'sdk/bindings/react/dist'), {recursive: true});
  sdk.pack(f.source, path.join(f.root, 'default'), version);
  const manifest = sdk.readPackage(path.join(f.root, 'default'));
  assert.equal(manifest.optionalGuiBindings, undefined);
  assert.equal(manifest.files.some(file => file.path.includes('/dist/')), false);
});
test('opt-in source package carries compiled imports through installation and relocation', t => {
  const f = fixture(t), packed = path.join(f.root, 'packed'), installed = path.join(f.root, 'installed'), moved = path.join(f.root, 'relocated');
  const cli = cp.spawnSync(process.execPath, [path.join(repository, 'tools/sdk/soui-sdk.cjs'), 'pack', '--source', f.source,
    '--output', packed, '--version', version, '--with-gui-bindings'], {encoding: 'utf8', windowsHide: true});
  assert.equal(cli.status, 0, cli.stderr);
  const manifest = sdk.readPackage(packed);
  assert.equal(manifest.optionalGuiBindings.packages.length, 3);
  for (const entry of gui.packages) {
    assert.ok(manifest.files.some(file => file.path === `${entry.root}/${entry.import}`));
    assert.ok(manifest.files.some(file => file.path === `${entry.root}/${entry.types}`));
  }
  sdk.install(packed, installed);
  fs.renameSync(installed, moved);
  assert.equal(gui.verify(moved).sdkVersion, version);
  const verified = cp.spawnSync(process.execPath, [path.join(repository, 'tools/sdk/soui-sdk.cjs'), 'verify-source', '--package', packed, '--source', moved],
    {encoding: 'utf8', windowsHide: true});
  assert.equal(verified.status, 0, verified.stderr);
});
test('stale source, missing output, mismatched SDK version, and output mutation fail before publication', t => {
  for (const [name, mutate] of [
    ['stale', f => f.write('sdk/bindings/shared/controller.ts', 'export const shared = false;\n')],
    ['missing', f => fs.unlinkSync(path.join(f.source, gui.packages[1].root, gui.packages[1].import))],
    ['version', f => {const name = 'sdk/bindings/react/package.json', meta = JSON.parse(fs.readFileSync(path.join(f.source, name))); meta.version = '0.4.0-preview.3'; f.write(name, identity.encoded(meta));}],
    ['mutation', f => f.write('sdk/web/dist/index.js', 'export const compiled = false;\n')]
  ]) {
    const f = fixture(t), output = path.join(f.root, name); mutate(f);
    assert.throws(() => sdk.pack(f.source, output, version, {withGuiBindings: true}));
    assert.equal(fs.existsSync(output), false);
  }
});
test('delivered missing, changed and undeclared compiled files are rejected', t => {
  for (const mutate of [
    dir => fs.unlinkSync(path.join(dir, 'sdk/web/dist/index.d.ts')),
    dir => fs.writeFileSync(path.join(dir, 'sdk/bindings/vue/dist/vue/src/index.js'), 'tampered'),
    dir => fs.writeFileSync(path.join(dir, 'sdk/bindings/react/dist/unknown.js'), 'unknown')
  ]) {
    const f = fixture(t), packed = path.join(f.root, 'packed'); sdk.pack(f.source, packed, version, {withGuiBindings: true});
    mutate(path.join(packed, 'payload'));
    assert.throws(() => sdk.readPackage(packed));
  }
});
test('links and hard-linked output cannot be consumed as GUI delivery', t => {
  const f = fixture(t), output = path.join(f.source, 'sdk/web/dist/index.js');
  fs.linkSync(output, path.join(f.root, 'output-hardlink'));
  assert.throws(() => gui.verify(f.source), /independent regular file/);
});
test('a failed compile command or modified raw compile log cannot become a verified build', t => {
  const f = fixture(t), report = JSON.parse(fs.readFileSync(path.join(f.source, gui.reportName)));
  report.commands[2].exitCode = 1;
  f.write(gui.reportName, identity.encoded(report));
  assert.throws(() => gui.verify(f.source), /compile command/);
  f.record();
  const current = JSON.parse(fs.readFileSync(path.join(f.source, gui.reportName)));
  f.write(`${current.evidenceDirectory}/${current.commands[0].stdout}`, 'changed');
  assert.throws(() => gui.verify(f.source), /compile log changed/);
});
test('a refreshed build record cannot replace the installed optional SDK lock', t => {
  const f = fixture(t), packed = path.join(f.root, 'fixed-package'), installed = path.join(f.root, 'fixed-project');
  sdk.pack(f.source, packed, version, {withGuiBindings: true});
  sdk.install(packed, installed);
  f.record(installed);
  assert.equal(gui.verify(installed).sdkVersion, version, 'A matching fixed lock and build record both pass');
  const reportFile = path.join(installed, gui.reportName), report = JSON.parse(fs.readFileSync(reportFile));
  report.commands[0].exitCode = 1;
  fs.writeFileSync(reportFile, identity.encoded(report));
  assert.throws(() => gui.verify(installed), /compile command/, 'A valid lock cannot hide a failed build record');
  f.record(installed);
  const output = path.join(installed, 'sdk/web/dist/index.js');
  fs.writeFileSync(output, 'export const compiled = false;\n');
  f.record(installed);
  assert.throws(() => sdk.readLock(installed), /modified/);
  assert.throws(() => gui.verify(installed), /modified|identity changed/,
    'A valid refreshed source/output record must not hide bytes changed from the fixed package');
});
test('a source-only fixed SDK allows unmanaged dist but still checks every managed file', t => {
  const f = fixture(t), packed = path.join(f.root, 'source-package'), installed = path.join(f.root, 'source-project');
  sdk.pack(f.source, packed, version);
  sdk.install(packed, installed);
  for (const entry of gui.packages) fs.cpSync(path.join(f.source, entry.root, 'dist'), path.join(installed, entry.root, 'dist'), {recursive: true});
  fs.writeFileSync(path.join(installed, 'sdk/web/dist/index.js'), 'export const compiled = false;\n');
  f.record(installed);
  assert.equal(gui.verify(installed).sdkVersion, version, 'Source-only SDK locks do not manage generated dist');
  assert.equal(sdk.readLock(installed).manifest.optionalGuiBindings, undefined);
  fs.writeFileSync(path.join(installed, 'cmake/fixture.txt'), 'managed SDK change\n');
  f.record(installed);
  assert.throws(() => sdk.readLock(installed), /modified/);
  assert.throws(() => gui.verify(installed), /modified/, 'A build record must not bypass other managed SDK files');
});
