'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const {spawn} = require('node:child_process');
const id = require('../../tools/internal/delivery-identity.cjs');
const builds = require('../../tools/internal/build-evidence.cjs');
const root = path.resolve(__dirname, '../..');
const target = 'SouiWebView2Consumer';

function write(file, value) { fs.writeFileSync(file, id.encoded(value), {flag:'wx'}); }
function directory(suite) {
  const resolved = id.safePath(suite);
  if (path.dirname(resolved) !== path.join(root, 'out', 'webview2'))
    throw new Error('Build directory must be a direct child of out/webview2');
  return resolved;
}
function source() {
  return id.snapshot(__dirname, id.walk(__dirname, '', (_relative, leaf) => id.skipped.has(leaf))
    .filter(name => name === 'CMakeLists.txt' || /\.(cpp|h|js|cjs|json|ps1|html|css)$/i.test(name)), true);
}
function copyFiles(from, to, files) {
  for (const entry of files) {
    const destination = id.safePath(to, entry.path);
    fs.mkdirSync(path.dirname(destination), {recursive:true});
    fs.copyFileSync(id.safePath(from, entry.path), destination, fs.constants.COPYFILE_EXCL);
  }
}
function snapshot(folder) { return id.snapshot(folder, id.walk(folder)); }
function assertSnapshot(folder, expected, label) {
  if (!expected || snapshot(folder).sha256 !== expected.sha256) throw new Error(label + ' changed');
}
function prepare(suite, architecture, configuration, packageRoot) {
  suite = directory(suite);
  const sdk = builds.verifyBuild(root, architecture, configuration, true);
  const experiment = source();
  for (const file of ['CMakeLists.txt','main.cpp','webview2_adapter.cpp','native-preload.js','index.html','page.js','style.css'])
    if (!experiment.files.some(entry => entry.path === file)) throw new Error('Missing experiment input: ' + file);
  const dependency = id.readJson(path.join(__dirname, 'dependency.json'));
  const expectedPackage = path.join(root, 'out', 'webview2-deps', dependency.version, 'package');
  if (id.safePath(packageRoot) !== expectedPackage) throw new Error('Use the package directory returned by Restore.ps1');
  const archive = path.join(path.dirname(expectedPackage), `microsoft.web.webview2.${dependency.version}.nupkg`);
  if (id.hash(fs.readFileSync(archive)) !== dependency.sha256) throw new Error('Pinned WebView2 package hash differs');
  const dependencyFiles = id.snapshot(packageRoot, ['build/native/include/WebView2.h',
    `build/native/${architecture}/WebView2LoaderStatic.lib`, ...dependency.licenseFiles]);
  const web = id.readJson(path.join(root, 'out', 'web-sdk.json'));
  if (web.schema !== 'soui.web-build/v1' || web.sourceSha256 !== sdk.source.sha256 ||
      id.snapshot(root, id.walk(root, 'sdk/web/dist')).sha256 !== web.artifacts.sha256)
    throw new Error('Public Web SDK must match the current native SDK build source');
  copyFiles(__dirname, path.join(suite, 'source'), experiment.files);
  // Byte snapshots bind the exact copies, independently of normalized source identity.
  const copiedSource = snapshot(path.join(suite, 'source'));
  copyFiles(packageRoot, path.join(suite, 'dependency'), dependencyFiles.files);
  const jsonHeader = id.snapshot(path.join(root, 'third_party', 'include', 'json'), ['json.hpp']);
  copyFiles(path.join(root, 'third_party', 'include', 'json'), path.join(suite, 'json'), jsonHeader.files);
  const webRoot = path.join(suite, 'web');
  copyFiles(path.join(suite, 'source'), webRoot, experiment.files.filter(entry => ['index.html','page.js','style.css'].includes(entry.path)));
  const sdkWeb = snapshot(path.join(root, 'sdk', 'web', 'dist'));
  copyFiles(path.join(root, 'sdk', 'web', 'dist'), path.join(webRoot, 'sdk'), sdkWeb.files);
  const pending = {schema:'soui.webview2-experiment-inputs/v1', buildId:crypto.randomUUID(),
    version:sdk.version, architecture, configuration, sdkBuildId:sdk.buildId,
    sdkSourceSha256:sdk.source.sha256, sdkLibrary:sdk.library, experimentSource:experiment,
    sourceSha256:id.hash(id.encoded({sdkSourceSha256:sdk.source.sha256, experimentSourceSha256:experiment.sha256})),
    copiedSource, dependency, dependencyFiles, jsonHeader, sdkWeb, web:snapshot(webRoot),
    startedAt:new Date().toISOString(), releaseReady:false};
  write(path.join(suite, 'inputs.json'), pending);
  return {buildId:pending.buildId, sourceSha256:pending.sourceSha256};
}
function verifyInputs(suite, expected) {
  const sdk = builds.verifyBuild(root, expected.architecture, expected.configuration, true);
  if (sdk.buildId !== expected.sdkBuildId || sdk.source.sha256 !== expected.sdkSourceSha256 ||
      source().sha256 !== expected.experimentSource.sha256)
    throw new Error('SDK or experimental build inputs changed; prepare a fresh build');
  assertSnapshot(path.join(suite, 'source'), expected.copiedSource, 'Frozen experiment source');
  assertSnapshot(path.join(suite, 'dependency'), expected.dependencyFiles, 'Frozen WebView2 package inputs');
  assertSnapshot(path.join(suite, 'json'), expected.jsonHeader, 'Frozen JSON header');
  assertSnapshot(path.join(suite, 'web'), expected.web, 'Frozen Web resources');
  if (snapshot(path.join(root, 'sdk', 'web', 'dist')).sha256 !== expected.sdkWeb.sha256)
    throw new Error('Public Web SDK build output changed');
  return sdk;
}
function compilerIdentity(native) {
  const directory = path.join(native, 'CMakeFiles');
  const candidates = fs.readdirSync(directory).sort().map(version => path.join(directory, version, 'CMakeCXXCompiler.cmake'));
  const file = candidates.find(candidate => fs.existsSync(candidate));
  if (!file) return {status:'unavailable'};
  const text = fs.readFileSync(file, 'utf8');
  const value = key => text.match(new RegExp(`set\\(${key} "([^"\\r\\n]*)"\\)`))?.[1] || null;
  return {status:'recorded', id:value('CMAKE_CXX_COMPILER_ID'), version:value('CMAKE_CXX_COMPILER_VERSION'),
    metadataSha256:id.hash(fs.readFileSync(file)), metadataFile:path.relative(native, file).replaceAll('\\','/')};
}
function finish(suite) {
  suite = directory(suite);
  const inputs = id.readJson(path.join(suite, 'inputs.json'));
  const sdk = verifyInputs(suite, inputs);
  const install = path.join(suite, 'install');
  if (id.hash(fs.readFileSync(path.join(install, 'lib', inputs.configuration, 'soui_sdk.lib'))) !== sdk.library.files[0].sha256)
    throw new Error('Installed SDK library differs from the verified native build');
  for (const entry of id.walk(root, 'sdk/include'))
    if (id.hash(fs.readFileSync(path.join(root, entry))) !== id.hash(fs.readFileSync(path.join(install, entry.slice(4)))))
      throw new Error('Installed public SDK header differs: ' + entry);
  const nativeBin = path.join(suite, 'native', 'bin', inputs.configuration);
  const runtime = path.join(suite, 'runtime');
  fs.mkdirSync(runtime);
  copyFiles(nativeBin, runtime, id.snapshot(nativeBin, [`${target}.exe`, 'native-preload.js']).files);
  fs.cpSync(path.join(suite, 'web'), path.join(runtime, 'resources'), {recursive:true,errorOnExist:true,force:false});
  if (id.hash(fs.readFileSync(path.join(runtime, 'native-preload.js'))) !==
      id.hash(fs.readFileSync(path.join(suite, 'source', 'native-preload.js'))))
    throw new Error('Native preload differs from frozen source');
  const runtimeDependencyRoot = path.join(root, 'third_party', 'prebuilt', inputs.architecture, 'runtime');
  const runtimeDependencies = id.snapshot(runtimeDependencyRoot, id.walk(runtimeDependencyRoot).filter(file => /\.dll$/i.test(file)));
  // Windows loads app-local CRT dependencies beside the executable, not from
  // the source archive's vc/ucrt subdirectories.
  for (const entry of runtimeDependencies.files)
    fs.copyFileSync(id.safePath(runtimeDependencyRoot, entry.path), path.join(runtime, path.basename(entry.path)),
      fs.constants.COPYFILE_EXCL);
  copyFiles(path.join(suite, 'dependency'), path.join(runtime, 'licenses', 'webview2'),
    inputs.dependencyFiles.files.filter(entry => inputs.dependency.licenseFiles.includes(entry.path)));
  const result = {...inputs, schema:'soui.webview2-experiment-build/v1', compiler:compilerIdentity(path.join(suite, 'native')),
    installedSdk:snapshot(install), runtimeDependencies, payload:snapshot(runtime),
    environment:{platform:os.platform(),release:os.release(),architecture:os.arch(),node:process.version},
    finishedAt:new Date().toISOString(), releaseReady:false};
  write(path.join(suite, 'build.json'), result);
  return {buildId:result.buildId, sourceSha256:result.sourceSha256, payloadSha256:result.payload.sha256};
}
function verify(suite) {
  suite = directory(suite);
  const build = id.readJson(path.join(suite, 'build.json'));
  if (build.schema !== 'soui.webview2-experiment-build/v1') throw new Error('Completed experimental build evidence is required');
  verifyInputs(suite, build);
  assertSnapshot(path.join(suite, 'install'), build.installedSdk, 'Installed SDK');
  assertSnapshot(path.join(suite, 'runtime'), build.payload, 'Experimental runtime payload');
  return build;
}
function validateRecords(records, request, outcome) {
  if (!['reload','recreate'].includes(request.mode) || !Number.isSafeInteger(request.cycles) || request.cycles < 1 || request.cycles > 1000)
    throw new Error('Invalid lifecycle request');
  if (outcome.exitCode !== 0 || outcome.timedOut !== false || outcome.signal || outcome.spawnError ||
      !Number.isInteger(outcome.pid) || outcome.pid <= 0 || outcome.terminationConfirmed !== true)
    throw new Error('Native process failed, timed out, or did not confirm termination');
  const expected = [{event:'started'}];
  for (let cycle = 1; cycle <= request.cycles; cycle++) {
    expected.push({event:'ready', cycle});
    if (request.mode === 'recreate' || cycle === request.cycles) expected.push({event:'closed', cycle});
  }
  expected.push({event:'completed'});
  if (!Array.isArray(records) || records.length !== expected.length) throw new Error('Missing or duplicate lifecycle events');
  let elapsed = -1;
  let runtimeVersion;
  const sessions = new Set();
  for (let index = 0; index < records.length; index++) {
    const record = records[index];
    const wanted = expected[index];
    if (record.event !== wanted.event || ('cycle' in wanted && record.cycle !== wanted.cycle))
      throw new Error('Lifecycle events are missing, duplicated or out of order');
    if (record.mode !== request.mode || record.sdkVersion !== request.sdkVersion ||
        !Number.isSafeInteger(record.elapsedMs) || record.elapsedMs < elapsed)
      throw new Error('Native lifecycle metadata or monotonic timestamp differs');
    elapsed = record.elapsedMs;
    if (record.event === 'started' && (record.pid !== outcome.pid || record.cycles !== request.cycles))
      throw new Error('Started native process identity or cycle count differs');
    if (record.event === 'started' && record.windowVisible !== true)
      throw new Error('The native GUI probe did not confirm a visible test window');
    if (record.event === 'completed' && record.cycles !== request.cycles)
      throw new Error('Native completion cycle count differs');
    if (record.event === 'ready') {
      if (typeof record.runtimeVersion !== 'string' || !record.runtimeVersion.trim() || record.runtimeVersion.length > 128 ||
          (runtimeVersion !== undefined && record.runtimeVersion !== runtimeVersion))
        throw new Error('Actual WebView2 Runtime version is missing or changed');
      if (typeof record.sessionDigest !== 'string' || !/^[0-9a-f]{64}$/i.test(record.sessionDigest) || sessions.has(record.sessionDigest))
        throw new Error('Ready document session identity is missing or was reused');
      sessions.add(record.sessionDigest);
      runtimeVersion = record.runtimeVersion;
    }
  }
  return {completedCycles:request.cycles, runtimeVersion, distinctDocumentSessions:sessions.size,
    resourceGate:'not-evaluated', processTreeGate:'not-evaluated', releaseReady:false};
}
function recordsFrom(file) {
  if (fs.statSync(file).size > 64 * 1024 * 1024) throw new Error('Native event log exceeds its bounded reader limit');
  const text = fs.readFileSync(file, 'utf8');
  if (!text.endsWith('\n')) throw new Error('Native event log is empty or truncated');
  return text.trimEnd().split('\n').map(line => JSON.parse(line));
}
async function run(suite, mode, cycles, timeoutMs, stepTimeoutMs) {
  suite = directory(suite);
  if (!['reload','recreate'].includes(mode) || !Number.isSafeInteger(cycles) || cycles < 1 || cycles > 1000 ||
      !Number.isSafeInteger(timeoutMs) || timeoutMs < 1000 || timeoutMs > 43200000 ||
      !Number.isSafeInteger(stepTimeoutMs) || stepTimeoutMs < 1 || stepTimeoutMs > 120000)
    throw new Error('Invalid experimental run parameters');
  const build = id.readJson(path.join(suite, 'build.json'));
  const runId = crypto.randomUUID();
  const output = path.join(suite, 'runs', runId);
  fs.mkdirSync(output, {recursive:true});
  const request = {schema:'soui.webview2-experiment-request/v1', runId, buildId:build.buildId,
    sourceSha256:build.sourceSha256, payloadSha256:build.payload.sha256, sdkVersion:build.version,
    mode, cycles, timeoutMs, stepTimeoutMs, startedAt:new Date().toISOString(), releaseReady:false};
  write(path.join(output, 'request.json'), request);
  const log = path.join(output, 'events.jsonl');
  const result = {schema:'soui.webview2-experiment-run/v1', runId, request, passed:false, releaseReady:false};
  try {
    verify(suite);
    const stdout = fs.openSync(path.join(output, 'stdout.log'), 'wx');
    const stderr = fs.openSync(path.join(output, 'stderr.log'), 'wx');
    try {
      result.outcome = await new Promise(resolve => {
        const child = spawn(path.join(suite, 'runtime', `${target}.exe`), [
          '--resources', path.join(suite, 'runtime', 'resources'), '--profile', path.join(output, 'profile'),
          '--output', log, '--cycles', String(cycles), '--mode', mode, '--timeout-ms', String(stepTimeoutMs),
        ], {cwd:path.join(suite, 'runtime'), windowsHide:true, stdio:['ignore', stdout, stderr]});
        let timedOut = false;
        let spawnError = null;
        let terminationTimer;
        const finish = (exitCode, signal, terminationConfirmed) => {
          clearTimeout(timer);
          clearTimeout(terminationTimer);
          resolve({pid:child.pid ?? null, exitCode, signal, timedOut, spawnError, terminationConfirmed});
        };
        const timer = setTimeout(() => {
          timedOut = true;
          child.kill(); // Only this run's directly spawned native process.
          terminationTimer = setTimeout(() => { child.unref(); finish(null, null, false); }, 5000);
        }, timeoutMs);
        child.once('error', error => { spawnError = error.message; });
        child.once('close', (code, signal) => finish(code, signal, true));
      });
    } finally { fs.closeSync(stdout); fs.closeSync(stderr); }
    if (fs.existsSync(log) && fs.statSync(log).size <= 64 * 1024 * 1024)
      result.logSha256 = id.hash(fs.readFileSync(log));
    result.validation = validateRecords(recordsFrom(log), request, result.outcome);
    verify(suite);
    result.passed = true;
  } catch (error) { result.failure = error.message; }
  result.finishedAt = new Date().toISOString();
  write(path.join(output, 'result.json'), result);
  return {passed:result.passed, runId, mode, cycles, result:path.relative(root, path.join(output, 'result.json')).replaceAll('\\','/'),
    runtimeVersion:result.validation?.runtimeVersion, failure:result.failure, outcome:result.outcome, releaseReady:false};
}

module.exports = {validateRecords, recordsFrom};
if (require.main === module) (async () => {
  const [command, suite, ...args] = process.argv.slice(2);
  let result;
  if (command === 'prepare' && args.length === 3) result = prepare(suite, ...args);
  else if (command === 'finish' && !args.length) result = finish(suite);
  else if (command === 'verify' && !args.length) { const build = verify(suite); result = {buildId:build.buildId, verified:true}; }
  else if (command === 'run' && args.length === 4) result = await run(suite, args[0], ...args.slice(1).map(Number));
  else throw new Error('Invalid experimental evidence command');
  console.log(id.encoded(result));
  if (result.passed === false) process.exitCode = 1;
})().catch(error => { console.error(error.message); process.exitCode = 1; });
