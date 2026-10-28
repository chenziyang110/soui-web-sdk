import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { spawnSync } from 'node:child_process';
import { fileURLToPath } from 'node:url';
import { createRequire } from 'node:module';

const directory = fileURLToPath(new URL('./', import.meta.url));
const repository = path.resolve(directory, '../..');
const require = createRequire(import.meta.url);
const sdkDelivery = require('../../tools/sdk/soui-sdk.cjs');
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const npmCli = process.env.npm_execpath || path.join(path.dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js');
function run(script, args, cwd) {
  const result = spawnSync(process.execPath, [script, ...args], { cwd, encoding: 'utf8', timeout: 30000 });
  assert.equal(result.status, 0, `${result.error || ''}\n${result.stderr}\n${result.stdout}`);
  return result.stdout;
}

test('binding delivery is peer-only and local installed consumers have no source links', () => {
  for (const framework of ['react', 'vue']) {
    const name = `@soui/gui-${framework}`;
    const folder = path.join(repository, 'sdk/bindings', framework);
    const metadata = read(path.join(folder, 'package.json'));
    assert.equal(metadata.version, fs.readFileSync(path.join(repository, 'VERSION'), 'utf8').trim());
    assert.equal(metadata.dependencies, undefined);
    assert.deepEqual(Object.keys(metadata.peerDependencies).sort(), ['@soui/native-sdk', framework].sort());
    const installed = path.join(directory, 'node_modules', name);
    assert.equal(fs.lstatSync(installed).isSymbolicLink(), false);
    const entry = fs.readFileSync(path.join(installed, metadata.main), 'utf8');
    assert.equal(entry.includes("from '@soui/native-sdk'"), false, 'Core is a borrowed type, not a new runtime instance');
    assert.equal(fs.existsSync(path.join(installed, 'dist/shared/controller.js')), true);
  }
});

test('packed bindings compile in an offline consumer outside the SDK tree', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-gui-consumer-'));
  t.after(() => {
    const resolved = fs.realpathSync(root);
    assert.ok(resolved.startsWith(fs.realpathSync(os.tmpdir()) + path.sep));
    fs.rmSync(resolved, { recursive: true, force: true });
  });
  const packages = {};
  for (const [name, relative] of [['@soui/native-sdk', 'sdk/web'], ['@soui/gui-react', 'sdk/bindings/react'], ['@soui/gui-vue', 'sdk/bindings/vue']]) {
    const packed = JSON.parse(run(npmCli, ['pack', path.join(repository, relative), '--pack-destination', root, '--json', '--ignore-scripts'], directory))[0];
    assert.equal(packed.version, fs.readFileSync(path.join(repository, 'VERSION'), 'utf8').trim(), 'Consumer uses actual same-version source builds');
    assert.ok(packed.files.some(file => file.path.endsWith('index.d.ts')));
    if (name !== '@soui/native-sdk') {
      assert.ok(packed.files.some(file => file.path === 'dist/shared/controller.js'));
      assert.equal(packed.files.some(file => /node_modules|\.\.\//.test(file.path)), false);
      assert.ok(packed.size < 12000, 'A thin binding should not embed framework or SDK runtime');
    }
    packages[name] = `file:${path.join(root, packed.filename).replaceAll('\\', '/')}`;
  }
  const source = read(path.join(directory, 'package.json'));
  const dependencies = { ...packages, react: source.dependencies.react, 'react-dom': source.dependencies['react-dom'], vue: source.dependencies.vue,
    '@types/react': source.devDependencies['@types/react'], '@types/react-dom': source.devDependencies['@types/react-dom'], typescript: source.devDependencies.typescript };
  fs.writeFileSync(path.join(root, 'package.json'), JSON.stringify({ name: 'standalone-gui-consumer', version: '0.1.0', private: true, type: 'module', dependencies }));
  fs.copyFileSync(path.join(directory, 'consumer.ts'), path.join(root, 'consumer.ts'));
  fs.copyFileSync(path.join(directory, 'tsconfig.json'), path.join(root, 'tsconfig.json'));
  run(npmCli, ['install', '--offline', '--ignore-scripts', '--no-audit', '--no-fund'], root);
  for (const name of Object.keys(packages)) assert.equal(fs.lstatSync(path.join(root, 'node_modules', name)).isSymbolicLink(), false);
  run(path.join(root, 'node_modules/typescript/bin/tsc'), ['-p', path.join(root, 'tsconfig.json')], root);
  fs.writeFileSync(path.join(root, 'imports.mjs'), "import * as sdk from '@soui/native-sdk'; import {useSouiGuiNode as react} from '@soui/gui-react'; import {useSouiGuiNode as vue} from '@soui/gui-vue'; if(typeof sdk.SouiGui !== 'function' || typeof react !== 'function' || typeof vue !== 'function') throw Error('Missing delivered imports');\n");
  run(path.join(root, 'imports.mjs'), [], root);
});

test('optional SDK source package imports and types survive installation outside the repository', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-gui-source-delivery-'));
  t.after(() => {
    assert.ok(fs.realpathSync(root).startsWith(fs.realpathSync(os.tmpdir()) + path.sep));
    fs.rmSync(root, {recursive: true, force: true});
  });
  const version = fs.readFileSync(path.join(repository, 'VERSION'), 'utf8').trim();
  const packed = path.join(root, 'sdk-package'), installed = path.join(root, 'installed'), moved = path.join(root, 'moved');
  sdkDelivery.pack(repository, packed, version, {withGuiBindings: true});
  sdkDelivery.install(packed, installed);
  fs.renameSync(installed, moved);
  const manifest = sdkDelivery.readLock(moved).manifest;
  assert.equal(manifest.optionalGuiBindings.sdkVersion, version);
  const consumer = path.join(root, 'consumer'); fs.mkdirSync(consumer);
  const dependencies = {};
  for (const entry of manifest.optionalGuiBindings.packages) {
    const tarball = JSON.parse(run(npmCli, ['pack', path.join(moved, entry.root), '--pack-destination', consumer, '--json', '--ignore-scripts'], consumer))[0];
    assert.equal(tarball.version, version);
    assert.ok(tarball.files.some(file => file.path === entry.import));
    assert.ok(tarball.files.some(file => file.path === entry.types));
    dependencies[entry.name] = `file:${path.join(consumer, tarball.filename).replaceAll('\\', '/')}`;
  }
  const metadata = read(path.join(directory, 'package.json'));
  Object.assign(dependencies, {react: metadata.dependencies.react, 'react-dom': metadata.dependencies['react-dom'], vue: metadata.dependencies.vue,
    '@types/react': metadata.devDependencies['@types/react'], '@types/react-dom': metadata.devDependencies['@types/react-dom'], typescript: metadata.devDependencies.typescript});
  fs.writeFileSync(path.join(consumer, 'package.json'), JSON.stringify({name: 'relocated-gui-consumer', version: '0.1.0', private: true, type: 'module', dependencies}));
  fs.copyFileSync(path.join(directory, 'consumer.ts'), path.join(consumer, 'consumer.ts'));
  fs.copyFileSync(path.join(directory, 'tsconfig.json'), path.join(consumer, 'tsconfig.json'));
  run(npmCli, ['install', '--offline', '--ignore-scripts', '--no-audit', '--no-fund'], consumer);
  for (const name of manifest.optionalGuiBindings.packages.map(entry => entry.name)) assert.equal(fs.lstatSync(path.join(consumer, 'node_modules', name)).isSymbolicLink(), false);
  run(path.join(consumer, 'node_modules/typescript/bin/tsc'), ['-p', path.join(consumer, 'tsconfig.json')], consumer);
  fs.writeFileSync(path.join(consumer, 'imports.mjs'), "import {useSouiGuiNode as react} from '@soui/gui-react'; import {useSouiGuiNode as vue} from '@soui/gui-vue'; import {SouiGui} from '@soui/native-sdk'; if([react,vue,SouiGui].some(value=>typeof value!=='function')) throw Error('Missing relocated imports');\n");
  run(path.join(consumer, 'imports.mjs'), [], consumer);
});

test('npm peers accept the promised 0.5 tuple and reject unpromised prerelease tuples', t => {
  const root = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-gui-peer-'));
  t.after(() => {
    const resolved = fs.realpathSync(root);
    assert.ok(resolved.startsWith(fs.realpathSync(os.tmpdir()) + path.sep));
    fs.rmSync(resolved, { recursive: true, force: true });
  });
  const bindings = {};
  for (const framework of ['react', 'vue']) {
    const packed = JSON.parse(run(npmCli, ['pack', path.join(repository, 'sdk/bindings', framework),
      '--pack-destination', root, '--json', '--ignore-scripts'], directory))[0];
    bindings[`@soui/gui-${framework}`] = `file:${path.join(root, packed.filename).replaceAll('\\', '/')}`;
  }
  // Only temporary metadata changes: the dist remains the checked-out SDK's bytes.
  // This validates npm resolution, never certifies 0.5 SDK behavior or identity.
  for (const [version, accepts] of [['0.5.0-preview.1', true], ['0.5.0', true],
    ['0.4.1-preview.1', false], ['0.5.1-preview.1', true], ['0.5.2-preview.1', false], ['0.6.0', false]]) {
    const staging = path.join(root, `sdk-${version}`), consumer = path.join(root, `consumer-${version}`);
    fs.mkdirSync(staging); fs.mkdirSync(consumer);
    const metadata = { ...read(path.join(repository, 'sdk/web/package.json')), version };
    fs.writeFileSync(path.join(staging, 'package.json'), JSON.stringify(metadata));
    fs.cpSync(path.join(repository, 'sdk/web/dist'), path.join(staging, 'dist'), { recursive: true });
    const packed = JSON.parse(run(npmCli, ['pack', staging, '--pack-destination', root, '--json', '--ignore-scripts'], directory))[0];
    const dependencies = { ...bindings, '@soui/native-sdk': `file:${path.join(root, packed.filename).replaceAll('\\', '/')}`,
      react: '19.3.0', 'react-dom': '19.3.0', vue: '3.5.43' };
    fs.writeFileSync(path.join(consumer, 'package.json'), JSON.stringify({ name: 'peer-resolution-fixture',
      version: '0.1.0', private: true, type: 'module', dependencies }));
    const result = spawnSync(process.execPath, [npmCli, 'install', '--offline', '--ignore-scripts', '--no-audit', '--no-fund'],
      { cwd: consumer, encoding: 'utf8', timeout: 30000 });
    assert.equal(result.error, undefined);
    if (accepts) {
      assert.equal(result.status, 0, `${version}: ${result.stderr}\n${result.stdout}`);
      assert.equal(read(path.join(consumer, 'node_modules/@soui/native-sdk/package.json')).version, version);
      for (const framework of ['react', 'vue']) assert.equal(fs.lstatSync(path.join(consumer, 'node_modules/@soui', `gui-${framework}`)).isSymbolicLink(), false);
    } else {
      assert.notEqual(result.status, 0, `Unpromised SDK version ${version} was accepted`);
      assert.match(result.stderr, /ERESOLVE/);
    }
  }
});
