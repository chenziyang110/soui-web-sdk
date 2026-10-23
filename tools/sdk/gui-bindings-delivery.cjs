'use strict';
const versioning = require('../internal/sdk-version.cjs');
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const crypto = require('node:crypto');
const identity = require('../internal/delivery-identity.cjs');
const schema = 'soui.optional-gui-bindings/v1';
const recordSchema = 'soui.gui-bindings-build/v1';
const packages = [
  {name: '@soui/native-sdk', root: 'sdk/web', import: 'dist/index.js', types: 'dist/index.d.ts'},
  ...['react', 'vue'].map(framework => ({name: `@soui/gui-${framework}`, root: `sdk/bindings/${framework}`,
    import: `dist/${framework}/src/index.js`, types: `dist/${framework}/src/index.d.ts`, framework}))
];
const previousPeerRange = '>=0.4.0-0 <0.5.0 || >=0.5.0-0 <0.6.0';
const peerRange = previousPeerRange + ' || 0.6.0-preview.3';
const reportName = 'out/gui-bindings/build.json';
function regular(root, name) {
  const file = identity.safePath(root, name), stat = fs.lstatSync(file);
  if (!stat.isFile() || stat.nlink !== 1) throw new Error(`GUI delivery requires an independent regular file: ${name}`);
  return file;
}
function read(root, name) { return identity.readJson(regular(root, name)); }
function versionOf(root) {
  return fs.existsSync(identity.safePath(root, 'VERSION')) ? fs.readFileSync(regular(root, 'VERSION'), 'utf8').trim() :
    read(root, 'sdk/web/package.json').version;
}
function metadata(root, version = versionOf(root)) {
  if (!versioning.isVersion(version)) throw new Error('GUI delivery requires an exact SDK version');
  // Old installed packages keep their original peer declaration. Only the
  // stable lines and prerelease tuples covered by that declaration may use it.
  const acceptsPreviousPeer = /^0\.[45]\.(0|[1-9][0-9]*)$/.test(version) ||
    /^0\.[45]\.0-(preview|rc)\.(0|[1-9][0-9]*)$/.test(version);
  const acceptsCurrentPeer = acceptsPreviousPeer || version === '0.6.0-preview.3';
  for (const entry of packages) {
    const meta = read(root, `${entry.root}/package.json`), lock = read(root, `${entry.root}/package-lock.json`);
    if (meta.name !== entry.name || meta.version !== version || lock.version !== version ||
        lock.packages?.['']?.version !== version || meta.type !== 'module' ||
        meta.main !== `./${entry.import}` || meta.types !== `./${entry.types}` ||
        meta.exports?.['.']?.import !== meta.main || meta.exports?.['.']?.types !== meta.types ||
        (meta.dependencies && Object.keys(meta.dependencies).length)) throw new Error(`GUI package identity/imports differ: ${entry.name}`);
    if (entry.framework) {
      if (Object.keys(meta.peerDependencies || {}).sort().join(',') !== ['@soui/native-sdk', entry.framework].sort().join(',') ||
          (!(acceptsCurrentPeer && meta.peerDependencies['@soui/native-sdk'] === peerRange) &&
            !(acceptsPreviousPeer && meta.peerDependencies['@soui/native-sdk'] === previousPeerRange)) ||
          !meta.peerDependencies[entry.framework] ||
          meta.devDependencies?.['@soui/native-sdk'] !== 'file:../../web' ||
          lock.packages?.['node_modules/@soui/native-sdk']?.version !== version) {
        throw new Error(`GUI package must borrow the exact local SDK and framework peers: ${entry.name}`);
      }
    }
  }
  return version;
}
function sourceNames(root) {
  const names = ['sdk/web/src', 'sdk/bindings/shared', 'sdk/bindings/react/src', 'sdk/bindings/vue/src']
    .flatMap(name => identity.walk(root, name, (_, leaf) => identity.skipped.has(leaf)));
  for (const entry of packages) for (const file of ['package.json', 'package-lock.json', 'tsconfig.json']) names.push(`${entry.root}/${file}`);
  names.push('tools/sdk/gui-bindings-delivery.cjs');
  return names;
}
function source(root) {
  const names = sourceNames(root).concat('tools/internal/sdk-version.cjs');
  names.forEach(name => regular(root, name));
  return identity.snapshot(root, names, true);
}
// Read-only compatibility for the exact previous preview producer. New builds
// and packages always use source(root), including the current version helper.
function sourceForInstalledDeclaration(root, declared) {
  if (declared.source.files.some(file => file.path === 'tools/internal/sdk-version.cjs')) return source(root);
  if (versioning.channelOf(declared.sdkVersion) !== 'preview' ||
      fs.existsSync(identity.safePath(root, 'tools/internal/sdk-version.cjs'))) {
    throw new Error('Historical GUI source identity is limited to the previous preview producer');
  }
  const oldTool = identity.normalize(fs.readFileSync(regular(root, 'tools/sdk/gui-bindings-delivery.cjs')), 'lf');
  if (oldTool.length !== 13693 || identity.hash(oldTool) !== '26305269c19891cd5dfb6d781985114f7b6aa1d6437d22213d4cb6ba25c10ec3') {
    throw new Error('Historical GUI producer bytes differ');
  }
  const names = sourceNames(root);
  names.forEach(name => regular(root, name));
  return identity.snapshot(root, names, true);
}
function outputs(root) {
  const names = packages.flatMap(entry => identity.walk(root, `${entry.root}/dist`));
  for (const entry of packages) for (const file of [entry.import, entry.types,
    ...(entry.framework ? ['dist/shared/controller.js', 'dist/shared/controller.d.ts'] : [])]) {
    if (!names.includes(`${entry.root}/${file}`)) throw new Error(`GUI compiled entry missing: ${entry.root}/${file}`);
  }
  names.forEach(name => regular(root, name));
  return identity.snapshot(root, names);
}
function declaration(root, version, input, output) {
  return {schema, sdkVersion: version, source: input, output,
    packages: packages.map(({name, root: directory, import: entry, types}) => ({name, version, root: directory, import: entry, types}))};
}
function verifyBuildRecord(root, report, version, input, output) {
  if (report.schema !== recordSchema || report.version !== version || report.source?.sha256 !== input.sha256 ||
      report.output?.sha256 !== output.sha256 || !Array.isArray(report.commands) || report.commands.length !== 6 ||
      !/^out\/gui-bindings\/build-[a-f0-9-]+$/.test(report.evidenceDirectory)) throw new Error('GUI build source/output identity changed; rebuild with -WithGuiBindings');
  for (const [index, command] of report.commands.entries()) {
    const entry = packages[Math.floor(index / 2)], suffix = index % 2 === 0 ?
      ['ci', '--ignore-scripts', '--no-audit', '--no-fund', '--install-links'] : ['run', 'build'];
    if (command.cwd !== entry.root || command.exitCode !== 0 || command.error !== null || typeof command.executable !== 'string' ||
        !Array.isArray(command.args) || command.args.length !== suffix.length + 1 ||
        JSON.stringify(command.args.slice(1)) !== JSON.stringify(suffix)) throw new Error('Invalid GUI compile command record');
    for (const kind of ['stdout', 'stderr']) {
      const file = command[kind];
      if (typeof file !== 'string' || file.includes('/') || file.includes('\\')) throw new Error('Invalid GUI compile log');
      const data = fs.readFileSync(regular(root, `${report.evidenceDirectory}/${file}`)), bound = command.logs?.[kind];
      if (!bound || bound.bytes !== data.length || bound.sha256 !== identity.hash(data)) throw new Error('GUI compile log changed');
    }
  }
}
function validateDeclaration(value, manifest) {
  if (!value || value.schema !== schema || value.sdkVersion !== manifest.version ||
      JSON.stringify(value.packages) !== JSON.stringify(declaration('', manifest.version, null, null).packages)) {
    throw new Error('Invalid optional GUI package declaration');
  }
  const files = new Map(manifest.files.map(file => [file.path, file]));
  for (const [kind, snapshot] of [['source', value.source], ['output', value.output]]) {
    if (!snapshot || !Array.isArray(snapshot.files) || identity.hash(identity.encoded(snapshot.files)) !== snapshot.sha256 ||
        snapshot.bytes !== snapshot.files.reduce((n, file) => n + file.bytes, 0)) throw new Error(`Invalid GUI ${kind} identity`);
    identity.validateFiles(snapshot.files);
    for (const file of snapshot.files) {
      const bound = files.get(file.path);
      if (!bound || bound.bytes !== file.bytes || bound.sha256 !== file.sha256 ||
          (kind === 'output' && !packages.some(entry => file.path.startsWith(`${entry.root}/dist/`))) ||
          (kind === 'source' && (!['lf', 'none'].includes(file.normalization) ||
            bound.sourceNormalization !== file.normalization))) throw new Error(`GUI identity does not match SDK manifest: ${file.path}`);
    }
  }
  for (const entry of packages) for (const file of [entry.import, entry.types,
    ...(entry.framework ? ['dist/shared/controller.js', 'dist/shared/controller.d.ts'] : [])]) {
    if (!value.output.files.some(item => item.path === `${entry.root}/${file}`)) throw new Error('GUI declared imports are missing');
  }
  const allOutputs = manifest.files.filter(file => packages.some(entry => file.path.startsWith(`${entry.root}/dist/`))).map(file => file.path).sort();
  if (JSON.stringify(allOutputs) !== JSON.stringify(value.output.files.map(file => file.path).sort())) throw new Error('GUI compiled file set differs');
  return value;
}
function verify(root, version = versionOf(root), {forPackaging = false} = {}) {
  metadata(root, version);
  let installed;
  if (fs.existsSync(identity.safePath(root, 'sdk-lock.json'))) {
    const lock = require('./soui-sdk.cjs').readLock(root);
    if (lock.manifest.version !== version) throw new Error('Installed SDK version differs from GUI delivery');
    if (lock.manifest.optionalGuiBindings !== undefined) {
      installed = validateDeclaration(lock.manifest.optionalGuiBindings, lock.manifest);
    }
  }
  const input = installed && !forPackaging ? sourceForInstalledDeclaration(root, installed) : source(root), output = outputs(root);
  if (installed && (installed.source.sha256 !== input.sha256 || installed.output.sha256 !== output.sha256)) {
    throw new Error('Installed GUI delivery identity changed');
  }
  const filename = identity.safePath(root, reportName);
  if (fs.existsSync(filename)) {
    const report = read(root, reportName);
    verifyBuildRecord(root, report, version, input, output);
  } else if (!installed) {
    throw new Error('GUI build record or installed optional GUI declaration is required');
  }
  return declaration(root, version, input, output);
}
function verifyDelivered(root, declared) {
  metadata(root, declared.sdkVersion);
  if (sourceForInstalledDeclaration(root, declared).sha256 !== declared.source.sha256 || outputs(root).sha256 !== declared.output.sha256) {
    throw new Error('Delivered GUI source/output file set changed');
  }
}
function npm(root, args, cwd, directory, name) {
  const cli = process.env.npm_execpath || path.join(path.dirname(process.execPath), 'node_modules/npm/bin/npm-cli.js');
  const result = cp.spawnSync(process.execPath, [cli, ...args], {cwd: identity.safePath(root, cwd), encoding: 'utf8',
    timeout: 120000, windowsHide: true});
  const stdout = `${name}.stdout.log`, stderr = `${name}.stderr.log`;
  fs.writeFileSync(path.join(directory, stdout), result.stdout || '', {flag: 'wx'});
  fs.writeFileSync(path.join(directory, stderr), result.stderr || '', {flag: 'wx'});
  const command = {cwd, executable: process.execPath, args: [cli, ...args], exitCode: result.status,
    error: result.error?.message || null, stdout, stderr, logs: Object.fromEntries([['stdout', result.stdout || ''], ['stderr', result.stderr || '']]
      .map(([kind, text]) => [kind, {bytes: Buffer.byteLength(text), sha256: identity.hash(Buffer.from(text))}]))};
  fs.writeFileSync(path.join(directory, `${name}.json`), identity.encoded(command), {flag: 'wx'});
  if (result.status !== 0) throw new Error(`Optional GUI command failed: ${cwd}; inspect ${directory}`);
  return command;
}
function evidenceDirectory(root, name) {
  const directory = identity.safePath(root, `out/gui-bindings/${name}-${crypto.randomUUID()}`);
  fs.mkdirSync(directory, {recursive: true}); return directory;
}
function build(root) {
  root = identity.safePath(root);
  const version = metadata(root), input = source(root), directory = evidenceDirectory(root, 'build'), commands = [];
  for (const entry of packages) {
    const output = identity.safePath(root, `${entry.root}/dist`);
    // These exact workspace-generated directories alone are owned by this opt-in build.
    if (fs.existsSync(output)) { outputsForRemoval(root, `${entry.root}/dist`); fs.rmSync(output, {recursive: true}); }
    commands.push(npm(root, ['ci', '--ignore-scripts', '--no-audit', '--no-fund', '--install-links'], entry.root, directory, `${entry.name.replaceAll('/', '-')}-install`));
    if (entry.framework && read(root, `${entry.root}/node_modules/@soui/native-sdk/package.json`).version !== version) throw new Error('GUI compiler installed another SDK version');
    commands.push(npm(root, ['run', 'build'], entry.root, directory, `${entry.name.replaceAll('/', '-')}-build`));
  }
  if (source(root).sha256 !== input.sha256) throw new Error('GUI source changed during build');
  const output = outputs(root), report = {schema: recordSchema, version, source: input, output, commands,
    evidenceDirectory: path.relative(root, directory).replaceAll('\\', '/')};
  const target = identity.safePath(root, reportName), temporary = `${target}.${crypto.randomUUID()}.tmp`;
  fs.writeFileSync(temporary, identity.encoded(report), {flag: 'wx'});
  try { fs.renameSync(temporary, target); } finally { if (fs.existsSync(temporary)) fs.unlinkSync(temporary); }
  return verify(root, version);
}
function outputsForRemoval(root, name) {
  identity.walk(root, name).forEach(file => regular(root, file));
  if (!packages.some(entry => name === `${entry.root}/dist`)) throw new Error('Invalid GUI build output cleanup');
}
function test(root) {
  const declared = verify(root), directory = evidenceDirectory(root, 'tests'), commands = [];
  for (const entry of ['tests/gui-bindings', 'tests/gui-bindings/react18']) {
    commands.push(npm(root, ['ci', '--ignore-scripts', '--no-audit', '--no-fund', '--install-links'], entry, directory, entry.replaceAll('/', '-') + '-install'));
  }
  for (const script of ['test', 'test:react18', 'typecheck', 'typecheck:react18']) commands.push(npm(root, ['run', script], 'tests/gui-bindings', directory, script.replaceAll(':', '-')));
  for (const entry of ['sdk/bindings/react', 'sdk/bindings/vue', 'tests/gui-bindings', 'tests/gui-bindings/react18']) {
    commands.push(npm(root, ['audit', '--json'], entry, directory, entry.replaceAll('/', '-') + '-audit'));
  }
  verify(root);
  fs.writeFileSync(path.join(directory, 'result.json'), identity.encoded({schema: 'soui.gui-bindings-tests/v1', version: declared.sdkVersion,
    sourceSha256: declared.source.sha256, outputSha256: declared.output.sha256, commands, passed: true, desktopValidated: false}), {flag: 'wx'});
  return {passed: true, version: declared.sdkVersion, evidenceDirectory: directory, desktopValidated: false};
}
module.exports = {packages, metadata, source, outputs, declaration, validateDeclaration, verifyDelivered, verify, build, test, schema, reportName};
if (require.main === module) {
  try {
    const [command, flag, root, ...extra] = process.argv.slice(2);
    if (!['build', 'verify', 'test'].includes(command) || flag !== '--root' || !root || extra.length) throw new Error('Usage: gui-bindings-delivery.cjs build|verify|test --root DIR');
    process.stdout.write(identity.encoded(module.exports[command](path.resolve(root))));
  } catch (error) { process.stderr.write(`GUI bindings: ${error.message}\n`); process.exitCode = 1; }
}
