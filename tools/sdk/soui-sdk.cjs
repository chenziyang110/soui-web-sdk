#!/usr/bin/env node
'use strict';
const versioning = require('../internal/sdk-version.cjs');

// A source SDK keeps the existing relative C++/TypeScript build layout while
// making the upstream-owned files an explicit, independently versioned unit.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const identity = require('../internal/delivery-identity.cjs');
const {isHistoricalNativeBackup} = require('../internal/native-payload.cjs');
const guiBindings = require('./gui-bindings-delivery.cjs');

const PACKAGE = 'soui.source-sdk/v1';
const LOCK = 'soui.sdk-lock/v1';
const sdkId = 'soui-desktop-sdk';
const managedRoots = ['sdk', 'cmake', 'third_party/include', 'third_party/prebuilt',
  'third_party/sources/zlib', 'third_party/licenses', 'third_party/manifests', 'tools/sdk', 'tools/internal'];
const managedFiles = ['LICENSE.md', 'THIRD_PARTY_NOTICES.md', 'tools/Build.ps1', 'tools/Test-Modern.ps1', 'tools/Test-Auto.ps1',
  'tools/Soui.ps1', 'tools/Doctor.ps1', 'docs/developer-workflow.md',
  'docs/devtools.md', 'docs/licenses/v0.6.0-redistribution.md', 'docs/testing/v0.6.0-long-run-handoff.md',
  'docs/adr/0123-public-devtools-candidate.md',
  'docs/adr/0128-controlled-cpu-presentation.md',
  'docs/adr/0130-bounded-legacy-source-maps.md',
  'docs/adr/0131-native-view-lifetime-observation.md',
  'docs/adr/0134-controlled-inspector-keyboard-snapshot.md',
  'docs/adr/0135-owned-local-resource-origin.md',
  'docs/adr/0137-bounded-windows-tool-preparation.md',
  'benchmarks/protocol/host64-build-debug-observer.cpp',
  'benchmarks/protocol/host64-build-debug-observer/CMakeLists.txt'];
const hash = data => crypto.createHash('sha256').update(data).digest('hex');
const fileHash = file => hash(fs.readFileSync(file));
const json = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));
const encoded = value => JSON.stringify(value, null, 2) + '\n';
const comparable = value => process.platform === 'win32' ? value.toLowerCase() : value;
const inside = (candidate, root) => comparable(candidate).startsWith(comparable(root) + path.sep);

function relativeName(name) {
  if (typeof name !== 'string' || !name || name.includes('\\') || name.includes(':') ||
      name.startsWith('/') || name.split('/').some(part => !part || part === '.' || part === '..' ||
        /[<>"|?*\x00-\x1f]|[. ]$/.test(part) || /^(con|prn|aux|nul|com[1-9]|lpt[1-9])(?:\.|$)/i.test(part))) {
    throw new Error(`Invalid package path: ${name}`);
  }
  return name;
}

function managed(name) {
  return managedFiles.includes(name) || managedRoots.some(root => name.startsWith(root + '/'));
}

function safePath(root, relative = '') {
  const full = relative ? path.resolve(root, relativeName(relative)) : path.resolve(root);
  if (relative && !inside(full, path.resolve(root))) throw new Error('Path escapes root');
  // Reject links/junctions in both payloads and installation destinations.
  let cursor = full;
  while (true) {
    try {
      if (fs.lstatSync(cursor).isSymbolicLink()) throw new Error(`Linked path: ${cursor}`);
    } catch (error) { if (error.code !== 'ENOENT') throw error; }
    const parent = path.dirname(cursor);
    if (parent === cursor) break;
    cursor = parent;
  }
  return full;
}

function walk(root, relative) {
  const full = safePath(root, relative);
  if (fs.statSync(full).isFile()) return [relative];
  return fs.readdirSync(full).sort().filter(name => !['node_modules', 'dist', 'out', '.git', '.vs'].includes(name))
    .flatMap(name => walk(root, `${relative}/${name}`));
}

function sourceFiles(source, manifest) {
  const historical = new Set(manifest ? manifest.files.map(file => file.path) : []);
  return [...managedRoots, ...managedFiles].flatMap(relative => walk(source, relative))
    .filter(relative => !isHistoricalNativeBackup(relative) || historical.has(relative))
    .concat(manifest?.optionalGuiBindings ? guiBindings.outputs(source).files.map(file => file.path) : []).sort();
}

function validateManifest(manifest) {
  if (manifest.schema !== PACKAGE || manifest.id !== sdkId || !versioning.isVersion(manifest.version) || manifest.channel !== versioning.channelOf(manifest.version) ||
      manifest.releaseReady !== false ||
      !Array.isArray(manifest.files) || !manifest.files.length) throw new Error('Invalid source SDK manifest');
  const seen = new Set();
  for (const file of manifest.files) {
    relativeName(file.path);
    if (!managed(file.path) || seen.has(file.path.toLowerCase()) ||
        !/^[a-f0-9]{64}$/.test(file.sha256) || !Number.isSafeInteger(file.bytes) || file.bytes < 0 ||
        (file.sourceNormalization !== undefined && !['lf', 'none'].includes(file.sourceNormalization))) {
      throw new Error(`Invalid or duplicate SDK entry: ${file.path}`);
    }
    seen.add(file.path.toLowerCase());
  }
  if (manifest.optionalGuiBindings !== undefined) guiBindings.validateDeclaration(manifest.optionalGuiBindings, manifest);
  return manifest;
}

function verifyFiles(root, manifest, source = false) {
  for (const file of manifest.files) {
    const full = safePath(root, file.path);
    if (!fs.existsSync(full) || !fs.statSync(full).isFile()) throw new Error(`SDK file missing or modified: ${file.path}`);
    const data = identity.normalize(fs.readFileSync(full), source ? file.sourceNormalization || 'none' : 'none');
    if (data.length !== file.bytes || hash(data) !== file.sha256) {
      throw new Error(`SDK file missing or modified: ${file.path}`);
    }
  }
}

function readPackage(directory) {
  const root = safePath(directory);
  const manifest = validateManifest(json(safePath(root, 'sdk-manifest.json')));
  verifyFiles(safePath(root, 'payload'), manifest);
  if (manifest.optionalGuiBindings) guiBindings.verifyDelivered(safePath(root, 'payload'), manifest.optionalGuiBindings);
  return manifest;
}

function readInstalledLock(directory) {
  const lock = json(safePath(directory, 'sdk-lock.json'));
  if (lock.schema !== LOCK || lock.manifestSha256 !== hash(encoded(lock.manifest))) {
    throw new Error('Invalid SDK lock identity');
  }
  validateManifest(lock.manifest);
  verifyFiles(directory, lock.manifest);
  if (lock.manifest.optionalGuiBindings) guiBindings.verifyDelivered(directory, lock.manifest.optionalGuiBindings);
  return lock;
}

const transaction = require('./install-transaction.cjs')({safePath, validateManifest, verifyFiles, hash, encoded, sdkId,
  publishDirectory: publishStagedDirectory});
function readLock(directory) {
  transaction.assertSettled(safePath(directory));
  return readInstalledLock(directory);
}

function failureDetails(error) {
  return {message: error.message, code: error.code ?? null, errno: error.errno ?? null,
    syscall: error.syscall ?? null};
}

function publicationPathState(file) {
  try {
    const state = fs.lstatSync(file);
    return {kind: state.isSymbolicLink() ? 'link' : state.isDirectory() ? 'directory' :
      state.isFile() ? 'file' : 'other'};
  } catch (error) {
    return error.code === 'ENOENT' ? {kind: 'missing'} : {kind: 'unreadable', error: failureDetails(error)};
  }
}

function publishStagedDirectory(stage, output) {
  const delay = new Int32Array(new SharedArrayBuffer(4));
  const attempts = [];
  let totalRetryDelayMs = 0;
  for (let attempt = 0; ; ++attempt) {
    try {
      fs.renameSync(stage, output);
      return;
    } catch (error) {
      attempts.push({attempt: attempt + 1, ...failureDetails(error)});
      // Windows can briefly deny a directory rename after the staged files
      // have been read. Retry only that transient publication boundary.
      if (process.platform !== 'win32' || !['EPERM', 'EACCES'].includes(error.code) ||
          attempt >= 5 || !fs.existsSync(stage) || fs.existsSync(output)) {
        // Observe before cleanup, without attributing EPERM to a particular
        // lock owner or changing the original native error or retry policy.
        try {
          error.publication = {schema: 'soui.sdk-publication-failure/v1', stage, output,
            attempts, totalRetryDelayMs, stageState: publicationPathState(stage),
            outputState: publicationPathState(output), lockOwnerKnown: false};
        } catch (_) { /* Diagnostics must never hide the original failure. */ }
        throw error;
      }
      const retryDelayMs = 30 * (attempt + 1);
      Atomics.wait(delay, 0, 0, retryDelayMs);
      totalRetryDelayMs += retryDelayMs;
    }
  }
}

function pack(source, output, version, options = {}) {
  source = safePath(source);
  output = safePath(output);
  if (comparable(output) === comparable(source) ||
      (inside(output, source) && !inside(output, path.join(source, 'dist', 'sdk')))) {
    throw new Error('Package output must be outside source or under dist/sdk');
  }
  if (fs.existsSync(output)) throw new Error('Package output already exists');
  const optionalGuiBindings = options.withGuiBindings ? guiBindings.verify(source, version, {forPackaging: true}) : undefined;
  const compiled = new Set(optionalGuiBindings?.output.files.map(file => file.path) || []);
  const files = sourceFiles(source).concat([...compiled]).sort();
  const manifest = validateManifest({schema: PACKAGE, id: sdkId, version, channel: versioning.channelOf(version), releaseReady: false,
    format: 'source-with-native-dependencies',
    ...(optionalGuiBindings ? {optionalGuiBindings} : {}),
    applicationContract: 'Applications own app, web, contracts and resources. SDK builds independently using CMake and links through Soui::Sdk.',
    files: files.map(relative => {
      const raw = fs.readFileSync(safePath(source, relative));
      const sourceNormalization = compiled.has(relative) ? 'none' : identity.normalization(relative, raw);
      const data = identity.normalize(raw, sourceNormalization);
      return {path: relative, bytes: data.length, sha256: hash(data), sourceNormalization};
    })});
  const stage = `${output}.staging-${crypto.randomUUID()}`;
  if (options.retainFailedPublication !== undefined && typeof options.retainFailedPublication !== 'boolean') {
    throw new Error('retainFailedPublication must be boolean');
  }
  let failure, retainedStage = false;
  try {
    for (const file of manifest.files) {
      const target = safePath(stage, `payload/${file.path}`);
      fs.mkdirSync(path.dirname(target), {recursive: true});
      fs.writeFileSync(target, identity.normalize(fs.readFileSync(safePath(source, file.path)), file.sourceNormalization), {flag: 'wx'});
    }
    fs.writeFileSync(safePath(stage, 'sdk-manifest.json'), encoded(manifest), {flag: 'wx'});
    readPackage(stage);
    publishStagedDirectory(stage, output);
  } catch (error) {
    failure = error;
    // Opt-in investigation/recovery keeps only a fully verified package whose
    // publication was denied. Other errors retain the existing cleanup path.
    // The original failed call still throws and never becomes a successful pack.
    if (options.retainFailedPublication && error.publication && ['EPERM', 'EACCES'].includes(error.code)) {
      try {
        const bytes = fs.readFileSync(safePath(stage, 'sdk-manifest.json'));
        error.publication.recovery = {command: 'recover-publication', stage, output,
          manifestSha256: hash(bytes), stageRetained: true};
        retainedStage = true;
      } catch (_) { /* A failed observation must not replace the native error. */ }
    }
    throw error;
  } finally {
    try {
      if (!retainedStage && fs.existsSync(stage)) fs.rmSync(stage, {recursive: true});
    } catch (cleanupError) {
      if (!failure) throw cleanupError;
      // Preserve the first error and any stage contents left by failed cleanup.
      try { failure.cleanupFailure = {stage, ...failureDetails(cleanupError)}; } catch (_) {}
    }
  }
  return {status: 'packed', version, files: files.length, output, releaseReady: false};
}

// This is a new explicit operation after contention is removed, not an
// extension of the failed call's six attempts / 450ms retry budget.
function recoverPublication(stage, output, manifestSha256) {
  stage = safePath(stage); output = safePath(output);
  if (typeof manifestSha256 !== 'string' || !/^[a-f0-9]{64}$/.test(manifestSha256)) {
    throw new Error('Expected retained manifest SHA256');
  }
  const prefix = path.basename(output) + '.staging-';
  const suffix = path.basename(stage).slice(prefix.length);
  if (comparable(path.dirname(stage)) !== comparable(path.dirname(output)) ||
      comparable(path.basename(stage).slice(0, prefix.length)) !== comparable(prefix) ||
      !/^[a-f0-9]{8}-[a-f0-9]{4}-4[a-f0-9]{3}-[89ab][a-f0-9]{3}-[a-f0-9]{12}$/i.test(suffix)) {
    throw new Error('Recovery requires the original sibling staging directory');
  }
  if (fs.existsSync(output)) throw new Error('Publication output already exists');
  const manifestFile = safePath(stage, 'sdk-manifest.json'), info = fs.lstatSync(manifestFile);
  if (!info.isFile() || info.nlink !== 1 || info.size > 16 * 1024 * 1024) throw new Error('Invalid retained SDK manifest');
  const raw = fs.readFileSync(manifestFile);
  if (hash(raw) !== manifestSha256) throw new Error('Retained SDK manifest identity changed');
  const manifest = readPackage(stage);
  const expected = ['sdk-manifest.json', ...manifest.files.map(file => 'payload/' + file.path)].sort();
  if (JSON.stringify(identity.walk(stage).sort()) !== JSON.stringify(expected)) {
    throw new Error('Retained SDK file set differs from manifest');
  }
  if (!fs.readFileSync(manifestFile).equals(raw)) throw new Error('Retained SDK manifest identity changed');
  try { publishStagedDirectory(stage, output); }
  catch (error) {
    if (error.publication) error.publication.recovery = {command: 'recover-publication', stage, output,
      manifestSha256, stageRetained: true};
    throw error;
  }
  return {status: 'publication-recovered', version: manifest.version, files: manifest.files.length,
    output, manifestSha256, releaseReady: false};
}

function requireProfile(manifest, profile = 'compatibility') {
  if (!['compatibility', 'modern', 'auto'].includes(profile)) throw new Error('Unknown application runtime profile');
  if (['modern', 'auto'].includes(profile)) {
    const paths = new Set(manifest.files.map(file => file.path));
    for (const required of ['sdk/include/soui/host/webview2_host.h',
      'sdk/renderers/webview2/CMakeLists.txt', 'tools/internal/Restore-WebView2.ps1']) {
      if (!paths.has(required)) throw new Error(`SDK does not provide the modern profile: ${required}`);
    }
  }
  if (profile === 'auto') {
    const paths = new Set(manifest.files.map(file => file.path));
    for (const required of ['sdk/include/soui/host/auto_webview_host.h',
      'sdk/renderers/auto/auto_webview_host.cpp', 'sdk/include/soui/host/soui_miniblink_host.h',
      'tools/internal/runtime-files.json', 'tools/internal/auto-smoke.cjs', 'tools/Test-Auto.ps1']) {
      if (!paths.has(required)) throw new Error(`SDK does not provide the auto profile: ${required}`);
    }
    for (const arch of ['x86', 'x64']) if (!paths.has(`third_party/prebuilt/${arch}/miniblink/node.dll`))
      throw new Error(`SDK does not provide the auto fallback runtime: ${arch}`);
  }
}

function install(packageRoot, projectRoot) {
  packageRoot = safePath(packageRoot);
  projectRoot = safePath(projectRoot);
  if (comparable(projectRoot) === comparable(packageRoot) || inside(projectRoot, packageRoot) || inside(packageRoot, projectRoot)) {
    throw new Error('Package and project must be separate trees');
  }
  const manifest = readPackage(packageRoot);
  const projectFile = safePath(projectRoot, 'project.json');
  if (fs.existsSync(projectFile)) requireProfile(manifest, JSON.parse(fs.readFileSync(projectFile, 'utf8')).runtimeProfile);
  transaction.recover(projectRoot);
  const context = transaction.acquire(projectRoot, true);
  let previous;
  try {
    const lockPath = safePath(projectRoot, 'sdk-lock.json');
    previous = fs.existsSync(lockPath) ? readInstalledLock(projectRoot) : null;
    if (previous && previous.manifest.version === manifest.version &&
        previous.manifestSha256 !== hash(encoded(manifest))) throw new Error('SDK version cannot identify different contents');
    const previousLock = previous ? fs.readFileSync(lockPath, 'utf8') : null;
    const nextLock = {schema: LOCK, manifestSha256: hash(encoded(manifest)), manifest};
    const plan = transaction.prepare(context, previousLock, encoded(nextLock), packageRoot);
    transaction.apply(context, plan);
    transaction.recoverOwned(context);
  } catch (error) {
    try {
      transaction.recoverOwned(context);
    } catch (recoveryError) {
      throw new Error(`${error.message}; recovery required at ${context.root}: ${recoveryError.message}`);
    }
    throw error;
  }
  return {status: previous ? 'updated' : 'installed', version: manifest.version,
    files: manifest.files.length, projectRoot, releaseReady: false};
}

function main(argv) {
  const [command, ...rest] = argv;
  const options = {};
  for (let i = 0; i < rest.length; ++i) {
    if (['--with-gui-bindings', '--retain-failed-publication'].includes(rest[i])) {
      if (command !== 'pack' || options[rest[i]]) throw new Error('Pack flags are unique and only valid for pack');
      options[rest[i]] = true; continue;
    }
    if (!/^--[a-z][a-z0-9-]*$/.test(rest[i]) || !rest[i + 1] || rest[i + 1].startsWith('--') || options[rest[i]]) {
      throw new Error('Expected unique --name value arguments');
    }
    options[rest[i]] = rest[++i];
  }
  const surfaces = {pack: ['--source', '--output', '--version'], verify: ['--package'],
    'recover-publication': ['--stage', '--output', '--manifest-sha256'],
    install: ['--package', '--project'], recover: ['--project'], 'verify-project': ['--project'],
    'verify-source': ['--package', '--source'], 'verify-profile': ['--package', '--profile']};
  const keys = surfaces[command];
  if (!keys || keys.some(key => !options[key]) || Object.keys(options).some(key => !keys.includes(key) && !(command === 'pack' && ['--with-gui-bindings', '--retain-failed-publication'].includes(key)))) {
    throw new Error('Commands: pack --source DIR --output DIR --version X.Y.Z[-preview.N|-rc.N] [--with-gui-bindings] [--retain-failed-publication] | recover-publication --stage DIR --output DIR --manifest-sha256 SHA256 | verify --package DIR | install --package DIR --project DIR | recover --project DIR | verify-project --project DIR | verify-source --package DIR --source DIR | verify-profile --package DIR --profile compatibility|modern|auto');
  }
  if (command === 'pack') return pack(options['--source'], options['--output'], options['--version'], {
    withGuiBindings: options['--with-gui-bindings'] === true, retainFailedPublication: options['--retain-failed-publication'] === true});
  if (command === 'recover-publication') return recoverPublication(options['--stage'], options['--output'], options['--manifest-sha256']);
  if (command === 'install') return install(options['--package'], options['--project']);
  if (command === 'verify-profile') {
    requireProfile(readPackage(options['--package']), options['--profile']);
    return {status: 'profile-available', profile: options['--profile']};
  }
  if (command === 'recover') return transaction.recover(safePath(options['--project']));
  if (command === 'verify-source') {
    const manifest = readPackage(options['--package']);
    const source = safePath(options['--source']);
    verifyFiles(source, manifest, true);
    const paths = sourceFiles(source, manifest);
    if (JSON.stringify(paths) !== JSON.stringify(manifest.files.map(file => file.path))) {
      throw new Error('SDK source file set changed; create a new SDK version');
    }
    return {status: 'source-matches-package', version: manifest.version};
  }
  const manifest = command === 'verify' ? readPackage(options['--package']) : readLock(options['--project']).manifest;
  return {status: 'verified', version: manifest.version, files: manifest.files.length, releaseReady: false};
}

module.exports = {pack, recoverPublication, install, recover: transaction.recover, readPackage, readLock, managedRoots, managedFiles, requireProfile};
if (require.main === module) {
  try { process.stdout.write(encoded(main(process.argv.slice(2)))); }
  catch (error) {
    process.stderr.write(`SDK: ${error.message}\n`);
    if (error.publication || error.cleanupFailure) {
      process.stderr.write(`SDK diagnostics: ${JSON.stringify({publication: error.publication,
        cleanupFailure: error.cleanupFailure})}\n`);
    }
    process.exitCode = 1;
  }
}
