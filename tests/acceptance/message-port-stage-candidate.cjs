'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {spawnSync} = require('node:child_process');

const hash = file => {
  const h = crypto.createHash('sha256'), buffer = Buffer.allocUnsafe(1024 * 1024);
  const fd = fs.openSync(file, 'r');
  try { for (;;) { const n = fs.readSync(fd, buffer, 0, buffer.length, null); if (!n) break; h.update(buffer.subarray(0, n)); } }
  finally { fs.closeSync(fd); }
  return h.digest('hex');
};
const need = (condition, why) => { if (!condition) throw new Error(why); };
const pin = file => ({path: file, bytes: fs.statSync(file).size, sha256: hash(file)});
function check(row) {
  need(row && path.isAbsolute(row.path), 'Absolute real producer pin required');
  const s = fs.lstatSync(row.path);
  need(s.isFile() && !s.isSymbolicLink() && s.size === row.bytes && hash(row.path) === row.sha256, 'Frozen actual producer changed: ' + row.path);
}
function save(file, value) { fs.writeFileSync(file, JSON.stringify(value, null, 2) + '\n', {flag: 'wx'}); }
const read = file => JSON.parse(fs.readFileSync(file, 'utf8'));
const same = (left, right) => JSON.stringify(left) === JSON.stringify(right);
function copyFile(source, destination, copies) {
  const before = pin(source); fs.mkdirSync(path.dirname(destination), {recursive: true});
  fs.copyFileSync(source, destination, fs.constants.COPYFILE_EXCL);
  const after = pin(source), copy = pin(destination);
  need(before.bytes === after.bytes && before.sha256 === after.sha256 && before.bytes === copy.bytes && before.sha256 === copy.sha256, 'Shared raw producer copy differs');
  copies.push({original: before, originalAfterCopy: after, copy, rawExact: true, originalProducerPathRetained: true});
}
function copyTree(source, destination, copies) {
  need(!fs.existsSync(destination), 'Fresh shared destination required');
  fs.mkdirSync(destination, {recursive: true});
  for (const entry of fs.readdirSync(source, {withFileTypes: true})) {
    const from = path.join(source, entry.name), to = path.join(destination, entry.name);
    need(!entry.isSymbolicLink(), 'Shared producer reparse member');
    if (entry.isDirectory()) copyTree(from, to, copies);
    else { need(entry.isFile(), 'Shared producer member is not a file'); copyFile(from, to, copies); }
  }
}
function main() {
  const [gate, inputFile, expected, arch] = process.argv.slice(2);
  need(process.argv.length === 6 && gate === 'ROOT_SHARED_ORDINARY_CANDIDATE_STAGE_V1' && /^[a-f0-9]{64}$/.test(expected) && ['x64', 'x86'].includes(arch), 'Explicit frozen actual inputs and Root-owned job required');
  need(process.platform === 'win32' && process.arch === 'x64' && !process.env.NODE_OPTIONS && !process.env.NODE_PATH && !process.env.NODE_REPL_EXTERNAL_MODULE, 'Fixed clean Root environment required');
  need(hash(inputFile) === expected, 'Actual input table changed');
  const input = read(inputFile);
  need(input.schema === 'soui.ordinary-shared-candidate-stage.actual-inputs/v1' && input.preparationCompleted === true && input.producerClosureReviewed === true && input.completeKernelToolchainQualified === false && input.qualificationPassed === false && input.releaseReady === false, 'Only reviewed closed diagnostic producers may be staged');
  input.pins.forEach(check); check(input.tools.node); check(input.tools.cmake);
  need(hash(process.execPath) === input.tools.node.sha256, 'Actual fixed Node differs');
  const scope = input.architectures.find(row => row.architecture === arch);
  need(scope && input.architectures.filter(row => row.architecture === arch).length === 1, 'Unique actual architecture binding required');
  for (const row of [scope.baselineBuild, scope.sdkBuild, scope.consumerExe, scope.candidateKernel, scope.baselineKernel, scope.preparationSummary, scope.producerReview]) check(row);
  const summary = read(scope.preparationSummary.path);
  need(summary.completed === true && summary.failure === null, 'Actual original preparation did not complete');
  const producer = summary.stages.find(row => row.job.name === 'prepare-' + arch);
  need(producer && producer.accepted === true && producer.result.exitCode === 0 && producer.result.supervisorExitCode === 0 && producer.result.passed === true && producer.result.closureConfirmed === true && producer.result.terminal.reason === 'completed' && producer.result.terminal.terminatedOwnedJob === false && producer.result.terminal.activeProcesses === 0, 'Actual whole original prepare Job must close naturally');
  const sourceRoot = path.resolve(input.sourceRoot), baselineSuite = path.resolve(scope.baselineSuite);
  const suiteParent = path.join(sourceRoot, 'out', 'acceptance');
  need(path.dirname(baselineSuite) === suiteParent && /^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/.test(path.basename(baselineSuite)), 'Real original baseline suite must be direct child');
  const runner = require(path.join(sourceRoot, 'tests', 'acceptance', 'runner.cjs'));
  const identity = require(path.join(sourceRoot, 'tools', 'internal', 'delivery-identity.cjs'));
  const baseline = runner.verify(baselineSuite), sdk = read(scope.sdkBuild.path);
  need(baseline.architecture === arch && baseline.configuration === 'Release' && baseline.buildId === sdk.buildId && baseline.sourceSha256 === sdk.source.sha256 && baseline.kernel.sha256 === scope.baselineKernel.sha256 && baseline.consumerBuildId === scope.baselineConsumerBuildId, 'Current original SDK/baseline suite binding differs');
  const originalExe = path.join(baselineSuite, 'consumer', 'bin', 'Release', 'SouiAcceptance.exe');
  const runtimeExe = path.join(baselineSuite, 'runtime', 'SouiAcceptance.exe');
  need(path.resolve(scope.consumerExe.path) === originalExe && hash(originalExe) === scope.consumerExe.sha256 && hash(runtimeExe) === scope.consumerExe.sha256, 'Shared executable must be the actual built and staged original consumer');
  const suite = path.join(suiteParent, crypto.randomUUID());
  fs.mkdirSync(suite);
  const copies = [];
  save(path.join(suite, 'shared-stage-start.json'), {schema: 'soui.ordinary-shared-candidate-stage.start/v1', architecture: arch, baselineSuite, actualCandidateSuite: suite, actualInput: pin(inputFile), preparation: scope.preparationSummary, consumerProducerReview: scope.producerReview, sdkBuild: scope.sdkBuild, consumerExe: scope.consumerExe, candidateKernel: scope.candidateKernel, reusedCompilation: true, consumerRecompiled: false, webRebuilt: false, typecheckReexecuted: false, qualificationPassed: false, releaseReady: false});
  try {
    runner.prepare(suite, arch, 'Release', scope.candidateKernel.path, 'message-port');
    const pending = read(path.join(suite, 'pending.json'));
    need(pending.buildId === baseline.buildId && pending.sourceSha256 === baseline.sourceSha256 && same(pending.consumerSource, baseline.consumerSource) && same(pending.webSdk, baseline.webSdk) && pending.kernel.sha256 === scope.candidateKernel.sha256, 'Original candidate prepare inputs differ from shared baseline');
    const installed = spawnSync(input.tools.cmake.path, ['--install', path.join(sourceRoot, 'out', 'sdk-' + arch), '--config', 'Release', '--prefix', path.join(suite, 'install')], {cwd: sourceRoot, encoding: null, windowsHide: true, timeout: 120000, maxBuffer: 32 * 1024 * 1024});
    fs.writeFileSync(path.join(suite, 'install.stdout.raw'), installed.stdout || Buffer.alloc(0), {flag: 'wx'});
    fs.writeFileSync(path.join(suite, 'install.stderr.raw'), installed.stderr || Buffer.alloc(0), {flag: 'wx'});
    save(path.join(suite, 'shared-install-result.json'), {executable: input.tools.cmake, args: ['--install', path.join(sourceRoot, 'out', 'sdk-' + arch), '--config', 'Release', '--prefix', path.join(suite, 'install')], exitCode: installed.status, signal: installed.signal, error: installed.error ? installed.error.message : null, installWasActuallyInvoked: true, qualificationPassed: false});
    need(installed.status === 0 && !installed.signal && !installed.error, 'Actual candidate SDK install failed');
    copyFile(originalExe, path.join(suite, 'consumer', 'bin', 'Release', 'SouiAcceptance.exe'), copies);
    copyTree(path.join(baselineSuite, 'consumer', 'resources'), path.join(suite, 'consumer', 'resources'), copies);
    copyTree(path.join(baselineSuite, 'web-output'), path.join(suite, 'web-output'), copies);
    for (const name of ['web-build.json', 'web-typecheck.tsconfig.json', 'web-typecheck.log', 'web-build.log']) copyFile(path.join(baselineSuite, name), path.join(suite, name), copies);
    for (const name of ['configure.log', 'compile.log']) copyFile(path.join(baselineSuite, name), path.join(suite, name), copies);
    // These are raw copies of the baseline producer's logs. Their embedded
    // absolute paths and timestamps remain baseline paths; they are not new
    // configure, compiler or type-check invocations for the candidate suite.
    const staged = runner.stage(suite), candidate = runner.verify(suite);
    const installedBaseline = identity.snapshot(path.join(baselineSuite, 'install'), identity.walk(path.join(baselineSuite, 'install')));
    need(same(candidate.installedSdk, installedBaseline) && same(candidate.web, baseline.web), 'Current installed SDK or shared Web output differs');
    const baselineRawRuntime = identity.snapshot(path.join(baselineSuite, 'runtime'), identity.walk(path.join(baselineSuite, 'runtime')));
    const candidateRawRuntime = identity.snapshot(path.join(suite, 'runtime'), identity.walk(path.join(suite, 'runtime')));
    const left = new Map(baselineRawRuntime.files.map(row => [row.path, row])), right = new Map(candidateRawRuntime.files.map(row => [row.path, row]));
    need(same([...left.keys()], [...right.keys()]), 'Baseline/candidate runtime members differ');
    const differences = [];
    for (const [name, before] of left) {
      const after = right.get(name);
      if (before.bytes !== after.bytes || before.sha256 !== after.sha256) differences.push(name);
    }
    need(same(differences, ['node.dll']), 'A/B payload must differ only by the actual candidate kernel');
    input.pins.forEach(check); for (const row of [scope.sdkBuild, scope.consumerExe, scope.candidateKernel, scope.baselineKernel, scope.preparationSummary, scope.producerReview]) check(row);
    runner.verify(baselineSuite); runner.verify(suite);
    need(hash(inputFile) === expected, 'Actual input table changed during shared staging');
    save(path.join(suite, 'shared-stage-producer.json'), {schema: 'soui.ordinary-shared-candidate-stage.producer/v1', architecture: arch, baselineSuite, actualCandidateSuite: suite, actualInput: pin(inputFile), originalSdkBuild: scope.sdkBuild, originalConsumerProducer: scope.consumerExe, originalPreparation: scope.preparationSummary, producerClosureReview: scope.producerReview, originalBaselineBuild: scope.baselineBuild, candidatePending: pin(path.join(suite, 'pending.json')), candidateBuild: pin(path.join(suite, 'build.json')), candidateStageResult: staged, copies, baselineRawRuntime, candidateRawRuntime, rawCopiesRetainBaselineProducerPathsAndTimes: true, consumerRecompiled: false, webRebuilt: false, typecheckReexecuted: false, sdkInstallExecuted: true, actualPayloadDifferences: differences, runtimeBytesExceptKernelExact: true, sourceMatched: true, sdkMatched: true, nativeKernelBehaviorExecuted: false, resourceExecuted: false, completeKernelToolchainQualified: false, qualificationPassed: false, releaseReady: false});
    console.log(JSON.stringify({architecture: arch, baselineSuite, actualCandidateSuite: suite, producer: pin(path.join(suite, 'shared-stage-producer.json')), candidateBuild: pin(path.join(suite, 'build.json')), payloadDifferences: differences, resourceExecuted: false, qualificationPassed: false, releaseReady: false}));
  } catch (error) {
    save(path.join(suite, 'shared-stage-failure.json'), {architecture: arch, baselineSuite, actualCandidateSuite: suite, error: error.message, copies, consumerRecompiled: false, webRebuilt: false, resourcePassed: false, qualificationPassed: false, releaseReady: false});
    throw error;
  }
}
try { main(); } catch (error) { console.error(error.stack); process.exitCode = 1; }
