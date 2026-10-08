'use strict';
// File-only preparation and verification. This driver never starts a compiler,
// package manager, application, WebView or VM. Build.ps1 owns those later steps.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
// A build executes its own frozen driver copy, three levels below this root.
const ROOT = path.basename(__dirname) === 'drivers' &&
  path.basename(path.dirname(path.dirname(__dirname))) === 'attempts'
  ? path.resolve(__dirname, '../../..') : __dirname;
// v2 reuses the verified 937-file v1 input snapshot, never a mutable checkout.
const SDK = path.join(ROOT, 'inputs/source');
const EVIDENCE = 'C:/SOUI-SDK-production-validation/preview11-session-renewal-20260926';
const SOURCE = 'a0c1d7900d39555508288d5459e65d3852e543cf7ec3879126bd6af59d2c2550';
const COMMIT = 'bec71cef4135087b76ccb368db8544950c1806b8';
const VERSION = '0.3.0-preview.11';
const PARENT_FREEZE = '7e45bc6158db50362e088ba8a97a08e42e7e63e398dde62df1dbe6ce6721620d';
const INPUTS = path.join(ROOT, 'inputs');
const encode = value => JSON.stringify(value, null, 2) + '\n';
const hash = value => crypto.createHash('sha256').update(value).digest('hex');
const read = name => JSON.parse(fs.readFileSync(name, 'utf8').replace(/^\uFEFF/, ''));
const assert = (condition, message) => { if (!condition) throw Error(message); };
function local(root, relative) {
  assert(typeof relative === 'string' && relative.length && !relative.includes('\\') &&
    !relative.includes(':') && !path.isAbsolute(relative) &&
    relative.split('/').every(p => p && p !== '.' && p !== '..' && !/[<>"|?*\x00-\x1f]|[. ]$/.test(p)), 'Unsafe relative path');
  const result = path.resolve(root, relative);
  let cursor = result;
  while (true) {
    if (fs.existsSync(cursor)) assert(!fs.lstatSync(cursor).isSymbolicLink(), 'Linked path: ' + cursor);
    const parent = path.dirname(cursor); if (parent === cursor) break; cursor = parent;
  }
  return result;
}
function walk(root, relative = '') {
  const full = relative ? local(root, relative) : root;
  assert(!fs.lstatSync(full).isSymbolicLink(), 'Linked input: ' + full);
  if (fs.statSync(full).isFile()) return [relative];
  return fs.readdirSync(full).sort().flatMap(name => walk(root, relative ? relative + '/' + name : name));
}
function snapshot(root, names = walk(root)) {
  const files = [...names].sort().map(name => {
    const bytes = fs.readFileSync(local(root, name));
    return {path: name, bytes: bytes.length, sha256: hash(bytes)};
  });
  const seen = new Set();
  for (const item of files) { assert(!seen.has(item.path.toLowerCase()), 'Case-fold duplicate'); seen.add(item.path.toLowerCase()); }
  assert(files.length, 'Empty manifest');
  return {sha256: hash(encode(files)), bytes: files.reduce((sum, item) => sum + item.bytes, 0), files};
}
function verify(root, expected, exact = true) {
  assert(expected && expected.files && hash(encode(expected.files)) === expected.sha256, 'Invalid manifest');
  const actual = snapshot(root, exact ? walk(root) : expected.files.map(item => item.path));
  assert(actual.sha256 === expected.sha256, 'Files changed: ' + root);
  return actual;
}
function write(name, value) { fs.writeFileSync(name, encode(value), {flag: 'wx'}); }
function copyFiles(source, destination, names) {
  assert(!fs.existsSync(destination), 'Destination already exists: ' + destination);
  fs.mkdirSync(destination, {recursive: true});
  for (const name of names) {
    const from = local(source, name), to = local(destination, name);
    fs.mkdirSync(path.dirname(to), {recursive: true});
    fs.copyFileSync(from, to, fs.constants.COPYFILE_EXCL);
  }
  const expected = snapshot(source, names);
  verify(destination, expected);
  return expected;
}
function checkRecord(record, arch, sdkOnly) {
  assert(record.schema === 'soui.source-build/v2' && record.version === VERSION &&
    record.architecture === arch && record.configuration === 'Release' &&
    record.sdkOnly === sdkOnly && record.nativeBuild === 'passed' &&
    record.source.sha256 === SOURCE && record.source.files.length === 711,
    'Unexpected SDK build identity: ' + arch);
}
function identityTool() {
  const location = fs.existsSync(path.join(INPUTS, 'source/tools/internal/delivery-identity.cjs'))
    ? path.join(INPUTS, 'source/tools/internal/delivery-identity.cjs')
    : path.join(SDK, 'tools/internal/delivery-identity.cjs');
  return require(location);
}
function checkSdkSource() {
  const actual = identityTool().source(SDK);
  assert(actual.sha256 === SOURCE && actual.files.length === 711, 'Frozen SDK source changed');
  return actual;
}
function prepared() {
  const result = read(path.join(ROOT, 'prepared.json'));
  assert(result.schema === 'soui.real-renewal-preparation/v1' && result.sourceSha256 === SOURCE &&
    result.fixtureRevision === 2 && result.parent?.freezeSha256 === PARENT_FREEZE, 'Wrong v2 preparation');
  verify(INPUTS, result.inputs);
  verify(path.join(ROOT, 'toolchain'), result.toolchainSeed);
  const source = identityTool().source(path.join(INPUTS, 'source'));
  assert(source.sha256 === SOURCE && source.files.length === 711, 'Frozen source archive changed');
  return result;
}
function prepare() {
  assert(!fs.existsSync(INPUTS) && !fs.existsSync(path.join(ROOT, 'prepared.json')) &&
    !fs.existsSync(path.join(ROOT, 'toolchain')), 'Preparation exists; preserve it and use a new directory');
  const full = {}, sdk = {};
  for (const arch of ['x86', 'x64']) {
    full[arch] = read(path.join(EVIDENCE, arch + '-attempt1/full-build-build-Release.json'));
    sdk[arch] = read(path.join(EVIDENCE, arch + '-attempt1/sdk-build-build-Release.json'));
    checkRecord(full[arch], arch, false); checkRecord(sdk[arch], arch, true);
  }
  const expected = full.x86.source;
  const identityEntry = expected.files.find(item => item.path === 'tools/internal/delivery-identity.cjs');
  const identityRaw = fs.readFileSync(path.join(SDK, identityEntry.path));
  const identityNormalized = Buffer.from(identityRaw.toString('latin1').replace(/\r\n/g, '\n'), 'latin1');
  assert(hash(identityNormalized) === identityEntry.sha256, 'Identity helper changed');
  checkSdkSource();
  fs.mkdirSync(INPUTS);
  copyFiles(SDK, path.join(INPUTS, 'source'), expected.files.map(item => item.path));
  assert(identityTool().source(path.join(INPUTS, 'source')).sha256 === SOURCE, 'Source copy identity mismatch');
  const summary = read(path.join(EVIDENCE, 'summary.json'));
  assert(summary.version === VERSION && summary.sourceSha256 === SOURCE, 'Summary source mismatch');
  const records = ['source-inputs.json', 'summary.json'];
  const architectures = {};
  for (const arch of ['x86', 'x64']) {
    const record = summary.architectures.find(item => item.architecture === arch);
    assert(record && record.sdkBuildId === sdk[arch].buildId && record.fullBuildId === full[arch].buildId &&
      record.installedSdk.files.length === 35, 'Install provenance mismatch');
    const installRoot = path.join(EVIDENCE, arch + '-attempt1/sdk-install');
    verify(installRoot, record.installedSdk);
    const library = sdk[arch].library.files.find(item => item.path === 'Release/soui_sdk.lib');
    const installedLibrary = record.installedSdk.files.find(item => item.path === 'lib/Release/soui_sdk.lib');
    assert(library && installedLibrary && library.sha256 === installedLibrary.sha256 &&
      library.bytes === installedLibrary.bytes, 'Installed library differs from SDK build');
    const installed = copyFiles(installRoot, path.join(INPUTS, 'install', arch), record.installedSdk.files.map(item => item.path));
    const runtimeRoot = path.join(SDK, 'out', arch, 'bin/Release/SouiStarter');
    identityTool().verifyPayload(runtimeRoot, 'SouiStarter', full[arch].payloads.SouiStarter);
    const baseNames = full[arch].payloads.SouiStarter.files.map(item => item.path)
      .filter(name => /^[^/]+\.dll$/i.test(name) || name.startsWith('resources/legal/'));
    assert(baseNames.includes('node.dll') && baseNames.filter(name => name.startsWith('resources/legal/')).length === 5,
      'Missing runtime or legal inputs');
    const runtimeBase = copyFiles(runtimeRoot, path.join(INPUTS, 'runtime-base', arch), baseNames);
    for (const name of ['full-build-build-Release.json', 'sdk-build-build-Release.json',
      'full-build.result.json', 'sdk-build.result.json', 'install.result.json', 'install.log'])
      records.push(arch + '-attempt1/' + name);
    assert(read(path.join(EVIDENCE, arch + '-attempt1/install.result.json')).exitCode === 0, 'Install command did not pass');
    architectures[arch] = {fullBuildId: full[arch].buildId, sdkBuildId: sdk[arch].buildId,
      installedSdk: installed, starterPayloadSha256: full[arch].payloads.SouiStarter.sha256, runtimeBase};
  }
  copyFiles(EVIDENCE, path.join(INPUTS, 'records'), records);
  const webRecord = read(path.join(SDK, 'out/web-sdk.json'));
  assert(webRecord.sourceSha256 === SOURCE, 'Web SDK source mismatch');
  verify(SDK, webRecord.artifacts, false);
  const distNames = walk(path.join(SDK, 'sdk/web/dist')).map(name => 'sdk/web/dist/' + name);
  assert(JSON.stringify(distNames.sort()) === JSON.stringify(webRecord.artifacts.files.map(item => item.path).sort()),
    'Unexpected Web SDK dist files');
  copyFiles(path.join(SDK, 'sdk/web'), path.join(INPUTS, 'web-sdk'), ['package.json', ...walk(path.join(SDK, 'sdk/web/dist')).map(name => 'dist/' + name)]);
  fs.copyFileSync(path.join(SDK, 'out/web-sdk.json'), path.join(INPUTS, 'records/web-sdk.json'), fs.constants.COPYFILE_EXCL);
  const toolchainSeed = copyFiles(path.join(SDK, 'tests/acceptance'), path.join(ROOT, 'toolchain'), ['package.json', 'package-lock.json']);
  checkSdkSource();
  const result = {schema: 'soui.real-renewal-preparation/v1', preparedAt: new Date().toISOString(),
    sourceRoot: SDK, evidenceRoot: EVIDENCE, sourceSha256: SOURCE, sourceInputSha256: SOURCE, sourceFiles: 711,
    compiledCandidateCommit: COMMIT, version: VERSION, inputs: snapshot(INPUTS),
    toolchainSeed, architectures, webSdkArtifactsSha256: webRecord.artifacts.sha256,
    sourceUnchanged: true, compiled: false, guiExecuted: false, realRenewalVerified: false, releaseReady: false};
  write(path.join(ROOT, 'prepared.json'), result);
  console.log(encode({status: 'prepared', sourceSha256: SOURCE, inputFiles: result.inputs.files.length,
    inputBytes: result.inputs.bytes, inputSha256: result.inputs.sha256, guiExecuted: false}));
}
function attemptPath(value) {
  assert(typeof value === 'string' && /^[a-z0-9][a-z0-9-]{0,63}$/.test(value), 'Invalid attempt name');
  return path.join(ROOT, 'attempts', value);
}
const driverNames = ['Prepare.cjs', 'Build-Web.mjs', 'Build.ps1', 'Run.ps1', 'Verify-Run.cjs', 'Clone-V1.cjs'];
function checkAttempt(attempt) {
  const pending = read(path.join(attempt, 'pending.json'));
  assert(pending.schema === 'soui.real-renewal-attempt/v1' && pending.sourceSha256 === SOURCE &&
    pending.fixtureRevision === 2 && pending.parent?.freezeSha256 === PARENT_FREEZE, 'Wrong v2 attempt');
  const base = prepared();
  assert(base.inputs.sha256 === pending.inputsSha256, 'Preparation identity changed');
  verify(path.join(attempt, 'consumer-source'), pending.consumerSource);
  verify(path.join(attempt, 'drivers'), pending.drivers);
  verify(path.join(attempt, 'protocol'), pending.protocol);
  // Source files must stay frozen while building, including the parent-owned fixture.
  verify(path.join(ROOT, 'consumer'), pending.consumerSource);
  verify(ROOT, pending.drivers, false);
  verify(ROOT, pending.protocol, false);
  checkSdkSource();
  return pending;
}
function begin(arch, name, node, cmake, npmCli) {
  assert(arch === 'x86' || arch === 'x64', 'Invalid architecture');
  const base = prepared(), attempt = attemptPath(name);
  assert(fs.statSync(attempt).isDirectory() && fs.readdirSync(attempt).every(item => item === 'logs'),
    'Attempt already used or not initialized by Build.ps1');
  const before = checkSdkSource();
  const consumerSource = copyFiles(path.join(ROOT, 'consumer'), path.join(attempt, 'consumer-source'), walk(path.join(ROOT, 'consumer')));
  const drivers = copyFiles(ROOT, path.join(attempt, 'drivers'), driverNames);
  const protocolNames = ['PROTOCOL.md', 'Verify-Run.cjs', 'SelfTest-Verify-Run.cjs'];
  const protocol = copyFiles(ROOT, path.join(attempt, 'protocol'), protocolNames);
  copyFiles(path.join(ROOT, 'toolchain'), path.join(attempt, 'toolchain'), base.toolchainSeed.files.map(item => item.path));
  const tools = {};
  for (const [key, executable] of Object.entries({node, cmake, npmCli})) {
    assert(path.isAbsolute(executable) && fs.statSync(executable).isFile(), 'Missing tool: ' + key);
    const bytes = fs.readFileSync(executable);
    tools[key] = {path: executable, bytes: bytes.length, sha256: hash(bytes)};
  }
  write(path.join(attempt, 'pending.json'), {schema: 'soui.real-renewal-attempt/v1',
    startedAt: new Date().toISOString(), architecture: arch, configuration: 'Release',
    version: VERSION, fixtureRevision: 2, parent: base.parent, sourceSha256: SOURCE, inputsSha256: base.inputs.sha256,
    sourceBeforeSha256: before.sha256, consumerSource, drivers, protocol, tools,
    sdkBuildId: base.architectures[arch].sdkBuildId, installedSdkSha256: base.architectures[arch].installedSdk.sha256,
    fullBuildId: base.architectures[arch].fullBuildId, guiExecuted: false});
  checkAttempt(attempt);
  console.log(encode({status: 'attempt-prepared', attempt, architecture: arch}));
}
function freezeToolchain(name) {
  const attempt = attemptPath(name); checkAttempt(attempt);
  const result = snapshot(path.join(attempt, 'toolchain'));
  assert(result.files.some(item => item.path === 'node_modules/typescript/bin/tsc') &&
    result.files.some(item => item.path === 'node_modules/vite/package.json'), 'npm ci output missing');
  write(path.join(attempt, 'toolchain-input.json'), result);
  console.log(encode({status: 'toolchain-frozen', files: result.files.length, sha256: result.sha256}));
}
function checkTools(pending) {
  for (const tool of Object.values(pending.tools)) {
    const data = fs.readFileSync(tool.path);
    assert(data.length === tool.bytes && hash(data) === tool.sha256, 'Build tool changed: ' + tool.path);
  }
}
function deploy(name) {
  const attempt = attemptPath(name), pending = checkAttempt(attempt), base = prepared();
  checkTools(pending);
  verify(path.join(attempt, 'toolchain'), read(path.join(attempt, 'toolchain-input.json')));
  const web = read(path.join(attempt, 'web-build.json'));
  assert(web.sourceSha256 === SOURCE && web.consumerSourceSha256 === pending.consumerSource.sha256 &&
    web.typecheck.passed && web.classicEntry, 'Missing verified Web output');
  verify(path.join(attempt, 'web-output'), web.output);
  const arch = pending.architecture, runtime = path.join(attempt, 'runtime');
  const copied = copyFiles(path.join(INPUTS, 'runtime-base', arch), runtime, base.architectures[arch].runtimeBase.files.map(item => item.path));
  for (const file of web.output.files) {
    assert(!file.path.startsWith('legal/'), 'Web build may not replace SDK legal files');
    const target = local(runtime, 'resources/' + file.path);
    fs.mkdirSync(path.dirname(target), {recursive: true});
    fs.copyFileSync(local(path.join(attempt, 'web-output'), file.path), target, fs.constants.COPYFILE_EXCL);
  }
  const exe = path.join(attempt, 'native-build/bin/Release/InstalledRenewalProbe.exe');
  const data = fs.readFileSync(exe), pe = data.readUInt32LE(0x3c);
  assert(data.readUInt16LE(0) === 0x5a4d && data.readUInt32LE(pe) === 0x4550 &&
    data.readUInt16LE(pe + 4) === (arch === 'x86' ? 0x14c : 0x8664), 'Consumer PE architecture mismatch');
  fs.copyFileSync(exe, path.join(runtime, 'InstalledRenewalProbe.exe'), fs.constants.COPYFILE_EXCL);
  verify(runtime, copied, false);
  verify(path.join(attempt, 'web-output'), web.output);
  checkAttempt(attempt);
  const result = {schema: 'soui.real-renewal-runtime/v1', createdAt: new Date().toISOString(),
    architecture: arch, version: VERSION, sourceSha256: SOURCE, sdkBuildId: pending.sdkBuildId,
    fullBuildId: pending.fullBuildId, inputsSha256: pending.inputsSha256,
    installedSdkSha256: pending.installedSdkSha256, consumerSourceSha256: pending.consumerSource.sha256,
    driverSha256: pending.drivers.sha256, protocolSha256: pending.protocol.sha256,
    webOutputSha256: web.output.sha256, payload: snapshot(runtime),
    executable: 'InstalledRenewalProbe.exe', guiExecuted: false, realRenewalVerified: false, releaseReady: false};
  write(path.join(attempt, 'runtime-input.json'), result);
  console.log(encode({status: 'runtime-frozen', architecture: arch, files: result.payload.files.length, sha256: result.payload.sha256}));
}
function finalize(name) {
  const attempt = attemptPath(name), pending = checkAttempt(attempt);
  checkTools(pending);
  const runtime = read(path.join(attempt, 'runtime-input.json'));
  verify(path.join(attempt, 'runtime'), runtime.payload);
  verify(path.join(attempt, 'toolchain'), read(path.join(attempt, 'toolchain-input.json')));
  const selfCheck = read(path.join(attempt, 'logs/self-check.stdout.log'));
  assert(selfCheck.status === 'self-check-passed' && selfCheck.version === VERSION &&
    selfCheck.architecture === pending.architecture && selfCheck.records === 8 &&
    selfCheck.fixtureRevision === 2 && selfCheck.phaseCases === 45 &&
    selfCheck.guiExecuted === false && selfCheck.realRenewalVerified === false, 'Self-check output invalid');
  for (const step of ['prepare', 'node-version', 'cmake-version', 'npm-version', 'npm-ci',
    'toolchain-freeze', 'web-build', 'configure', 'native-build', 'deploy', 'self-check']) {
    const result = read(path.join(attempt, 'logs', step + '.result.json'));
    assert(result.exitCode === 0 && result.timedOut === false && !result.error, 'Failed build step: ' + step);
  }
  const captured = snapshot(attempt, ['pending.json', 'toolchain-input.json', 'web-build.json', 'runtime-input.json',
    'web-typecheck.tsconfig.json', 'vite-index-before-classic.html', 'native-build/CMakeCache.txt',
    ...walk(path.join(attempt, 'logs')).filter(name => !name.startsWith('finalize.')).map(name => 'logs/' + name)]);
  write(path.join(attempt, 'build.json'), {schema: 'soui.real-renewal-build/v1', finishedAt: new Date().toISOString(),
    architecture: pending.architecture, buildId: name, fixtureRevision: 2, parent: pending.parent,
    sourceSha256: SOURCE, sdkBuildId: pending.sdkBuildId,
    installedSdkSha256: pending.installedSdkSha256, consumerSource: pending.consumerSource,
    drivers: pending.drivers, protocol: pending.protocol, toolchain: read(path.join(attempt, 'toolchain-input.json')),
    tools: pending.tools, payload: runtime.payload, evidence: captured,
    sourceUnchanged: true, nativeBuild: 'passed', webBuild: 'passed', selfCheck: 'passed',
    guiExecuted: false, realRenewalVerified: false, releaseReady: false});
  console.log(encode({status: 'build-prepared-for-desktop', architecture: pending.architecture,
    runtime: path.join(attempt, 'runtime'), guiExecuted: false, realRenewalVerified: false}));
}
function freeze(x86Name, x64Name) {
  const base = prepared(); checkSdkSource();
  const result = {schema: 'soui.real-renewal-preparation/v1', status: 'compiled-inputs-frozen',
    createdAt: new Date().toISOString(), version: VERSION, fixtureRevision: 2, parent: base.parent, sourceInputSha256: SOURCE,
    compiledCandidateCommit: COMMIT, source: {root: path.join(INPUTS, 'source'),
      ...identityTool().source(path.join(INPUTS, 'source'))},
    inputs: {root: INPUTS, ...base.inputs}, runtimes: {}, toolchain: {}, consumer: {},
    guiExecuted: false, realRenewalVerified: false, releaseReady: false};
  for (const [arch, name] of [['x86', x86Name], ['x64', x64Name]]) {
    const attempt = attemptPath(name), pending = checkAttempt(attempt);
    assert(pending.architecture === arch, 'Freeze architecture does not match attempt');
    const build = read(path.join(attempt, 'build.json'));
    assert(build.schema === 'soui.real-renewal-build/v1' && build.fixtureRevision === 2 &&
      build.parent?.freezeSha256 === PARENT_FREEZE && build.architecture === arch && build.buildId === name &&
      build.nativeBuild === 'passed' && build.webBuild === 'passed' && build.selfCheck === 'passed' &&
      build.sourceUnchanged && build.sourceSha256 === SOURCE && build.sdkBuildId === pending.sdkBuildId,
      'Build is not eligible for runtime freeze');
    verify(path.join(attempt, 'runtime'), build.payload);
    verify(path.join(attempt, 'toolchain'), build.toolchain);
    verify(attempt, build.evidence, false);
    checkTools(pending);
    result.runtimes[arch] = {runtimePath: path.join(attempt, 'runtime'),
      payload: build.payload.files, payloadSha256: build.payload.sha256, buildId: name,
      sdkBuildId: pending.sdkBuildId, fullBuildId: pending.fullBuildId,
      buildRecordPath: path.join(attempt, 'build.json'), buildRecordSha256: hash(fs.readFileSync(path.join(attempt, 'build.json')))};
    result.toolchain[arch] = {root: path.join(attempt, 'toolchain'), ...build.toolchain, tools: build.tools};
    result.consumer[arch] = {root: path.join(attempt, 'consumer-source'), ...build.consumerSource};
  }
  write(path.join(ROOT, 'freeze.json'), result);
  console.log(encode({status: result.status, freeze: path.join(ROOT, 'freeze.json'),
    sha256: hash(fs.readFileSync(path.join(ROOT, 'freeze.json'))), guiExecuted: false}));
}
module.exports = {ROOT, INPUTS, SOURCE, VERSION, encode, hash, read, assert, local, walk, snapshot, verify, write, prepared, checkAttempt, attemptPath};
if (require.main === module) {
  try {
    const [command, ...args] = process.argv.slice(2);
    if (command === 'prepare' && args.length === 0) throw Error('v2 inputs are copied by Clone-V1.cjs, never rebuilt from the mutable SDK checkout');
    else if (command === 'verify' && args.length === 0) { const value = prepared(); checkSdkSource(); console.log(encode({status: 'verified', inputsSha256: value.inputs.sha256})); }
    else if (command === 'begin' && args.length === 5) begin(...args);
    else if (command === 'toolchain-freeze' && args.length === 1) freezeToolchain(...args);
    else if (command === 'deploy' && args.length === 1) deploy(...args);
    else if (command === 'finalize' && args.length === 1) finalize(...args);
    else if (command === 'freeze' && args.length === 2) freeze(...args);
    else throw Error('Usage: Prepare.cjs prepare|verify|begin ARCH ATTEMPT NODE CMAKE NPMCLI|toolchain-freeze ATTEMPT|deploy ATTEMPT|finalize ATTEMPT|freeze X86_ATTEMPT X64_ATTEMPT');
  } catch (error) { console.error(error.stack || String(error)); process.exitCode = 1; }
}
