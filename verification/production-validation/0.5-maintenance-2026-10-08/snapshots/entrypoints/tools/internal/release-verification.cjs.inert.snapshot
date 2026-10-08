'use strict';
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const id = require('./delivery-identity.cjs');
const buildEvidence = require('./build-evidence.cjs');
const sdk = require('../sdk/soui-sdk.cjs');
const root = path.resolve(__dirname, '../..');
const reportNames = ['generated-project.json', 'fresh-project.json', 'relocation.json', 'relocated-native-smoke.json'];
const hashPattern = /^[a-f0-9]{64}$/;
function requireTrue(value, message) { if (!value) throw new Error(message); }
function inside(candidate, parent) {
  const relative = path.relative(parent, candidate);
  return relative === '' || (!relative.startsWith('..' + path.sep) && relative !== '..' && !path.isAbsolute(relative));
}
function externalWorkspace(source, workspace) {
  const full = id.safePath(workspace);
  requireTrue(!inside(full, id.safePath(source)), 'Verification workspace must be outside the SDK source');
  requireTrue(!fs.existsSync(full), 'Verification workspace must be a new directory');
  return full;
}
function businessSnapshot(project) {
  const names = ['app', 'web', 'contracts', 'resources'].filter(name => fs.existsSync(id.safePath(project, name)))
    .flatMap(name => id.walk(project, name, (_relative, leaf) => id.skipped.has(leaf)));
  for (const name of ['CMakeLists.txt', 'project.json', 'VERSION', '.gitattributes', '.gitignore'])
    if (fs.existsSync(id.safePath(project, name))) names.push(name);
  return id.snapshot(project, names);
}
function validateStep(step, version) {
  const {build, smoke} = step;
  requireTrue(step.sdkVersion === version && hashPattern.test(step.sdkManifestSha256) &&
    hashPattern.test(step.businessSha256), 'Step SDK or business identity is invalid');
  requireTrue(build?.schema === 'soui.source-build/v2' && build.nativeBuild === 'passed' &&
    build.version === version && build.architecture === 'x64' && build.configuration === 'Release' &&
    build.sdkOnly === false && typeof build.buildId === 'string' && !!build.buildId &&
    hashPattern.test(build.source?.sha256), 'Step build identity is invalid');
  requireTrue(smoke?.schema === 'soui.native-smoke/v2' && smoke.passed === true && !!smoke.finishedAt &&
    smoke.version === version && smoke.architecture === 'x64' && smoke.configuration === 'Release' &&
    smoke.buildId === build.buildId && smoke.sourceSha256 === build.source.sha256 &&
    Array.isArray(smoke.runs) && smoke.runs.length === 1, 'Step smoke identity is invalid');
  const run = smoke.runs[0];
  requireTrue(run.passed === true && run.exitCode === 0 && run.timedOut === false &&
    run.scenario === 'default' && typeof run.log === 'string' &&
    run.log.includes('application-ready') && run.log.includes('host-close-complete'), 'Step did not actually become ready and close');
  return step;
}
function validateUpgrade(steps, oldVersion, version) {
  const names = ['baseline', 'upgrade', 'rollback', 'reupgrade'];
  requireTrue(typeof oldVersion === 'string' && oldVersion !== version, 'Upgrade must use a distinct prior version');
  requireTrue(Array.isArray(steps) && steps.length === names.length, 'Upgrade/rollback phases are missing');
  const ids = new Set();
  for (let index = 0; index < names.length; ++index) {
    const step = validateStep(steps[index], index % 2 ? version : oldVersion);
    requireTrue(step.name === names[index] && step.businessSha256 === steps[0].businessSha256,
      'Upgrade order or preserved business identity differs');
    requireTrue(!ids.has(step.build.buildId), 'Upgrade phases reused a build');
    ids.add(step.build.buildId);
  }
  requireTrue(steps[0].sdkManifestSha256 === steps[2].sdkManifestSha256 &&
    steps[1].sdkManifestSha256 === steps[3].sdkManifestSha256, 'Upgrade/rollback package identity differs');
}
function save(file, value) { fs.writeFileSync(id.safePath(file), id.encoded(value)); }
function readRun(file) {
  file = id.safePath(file);
  requireTrue(inside(file, path.join(root, 'out/release-validation')), 'Run file must be in release validation output');
  const run = id.readJson(file);
  requireTrue(run.schema === 'soui.release-validation-run/v1' && run.root === root &&
    run.version === buildEvidence.version(root) && run.rootSourceSha256 === id.source(root).sha256,
    'Release source changed during verification');
  const current = sdk.readPackage(run.package);
  const previous = sdk.readPackage(run.oldPackage);
  requireTrue(current.version === run.version && previous.version === run.oldVersion &&
    id.hash(id.encoded(current)) === run.sdkManifestSha256 &&
    id.hash(id.encoded(previous)) === run.oldSdkManifestSha256, 'Fixed SDK package changed during verification');
  return run;
}
function begin(file, workspace, packagePath, oldPackage) {
  file = id.safePath(file);
  requireTrue(inside(file, path.join(root, 'out/release-validation')) && !fs.existsSync(file),
    'Use a new run file in release validation output');
  const version = buildEvidence.version(root);
  // Invalidate before package/path validation or directory creation can fail.
  for (const name of reportNames) save(path.join(root, 'verification', name),
    {schema: 'soui.release-validation-pending/v1', sdkVersion: version, passed: false,
      releaseReady: false, startedAt: new Date().toISOString()});
  const current = sdk.readPackage(packagePath);
  const previous = sdk.readPackage(oldPackage);
  requireTrue(current.version === version && current.version !== previous.version, 'Use current and different prior SDK versions');
  const sourceCheck = cp.spawnSync(process.execPath, [path.join(root, 'tools/sdk/soui-sdk.cjs'),
    'verify-source', '--package', packagePath, '--source', root], {encoding: 'utf8', windowsHide: true});
  requireTrue(sourceCheck.status === 0, 'Current SDK package differs from source: ' + (sourceCheck.stderr || ''));
  const external = externalWorkspace(root, workspace);
  const run = {schema: 'soui.release-validation-run/v1', root, version, oldVersion: previous.version,
    rootSourceSha256: id.source(root).sha256, package: id.safePath(packagePath), oldPackage: id.safePath(oldPackage),
    sdkManifestSha256: id.hash(id.encoded(current)), oldSdkManifestSha256: id.hash(id.encoded(previous)),
    workspace: external, startedAt: new Date().toISOString(), steps: []};
  fs.mkdirSync(path.dirname(id.safePath(file)), {recursive: true});
  save(file, run);
  fs.mkdirSync(external);
  return {version, oldVersion: run.oldVersion, workspace: external};
}
function projectFor(run, name) {
  const directory = name === 'fresh' ? 'fresh' : name === 'relocation' ? 'relocated' : 'upgrade';
  return id.safePath(run.workspace, directory);
}
function setBusiness(file) {
  const run = readRun(file);
  requireTrue(!run.upgradeBusiness, 'Upgrade business identity was already captured');
  run.upgradeBusiness = businessSnapshot(projectFor(run, 'baseline'));
  save(file, run);
  return {businessSha256: run.upgradeBusiness.sha256, files: run.upgradeBusiness.files.length};
}
function record(file, name) {
  const run = readRun(file);
  requireTrue(['fresh', 'baseline', 'upgrade', 'rollback', 'reupgrade', 'relocation'].includes(name) &&
    !run.steps.some(step => step.name === name), 'Unknown or duplicate release phase');
  const project = projectFor(run, name);
  const lock = sdk.readLock(project);
  const version = ['baseline', 'rollback'].includes(name) ? run.oldVersion : run.version;
  const {build, smoke} = buildEvidence.verifySmoke(project, 'x64', 'Release');
  const business = businessSnapshot(project);
  const step = {name, sdkVersion: lock.manifest.version, sdkManifestSha256: lock.manifestSha256,
    businessSha256: business.sha256, build, smoke};
  validateStep(step, version);
  requireTrue(step.sdkManifestSha256 === (version === run.oldVersion ? run.oldSdkManifestSha256 : run.sdkManifestSha256),
    'Project installed an unexpected SDK package');
  if (!['fresh', 'relocation'].includes(name)) requireTrue(run.upgradeBusiness &&
    business.sha256 === run.upgradeBusiness.sha256, 'SDK installation overwrote application business files');
  run.steps.push(step);
  save(file, run);
  return {name, sdkVersion: version, buildId: build.buildId, sourceSha256: build.source.sha256, passed: true};
}
function copyFiles(file) {
  const run = readRun(file), fresh = projectFor(run, 'fresh');
  requireTrue(run.steps.some(step => step.name === 'fresh'), 'Validate the fresh project before copying it');
  const lock = sdk.readLock(fresh);
  const names = new Set([...id.source(fresh).files.map(entry => entry.path), ...lock.manifest.files.map(entry => entry.path),
    '.gitattributes', '.gitignore', 'sdk-lock.json']);
  return [...names].sort().filter(name => fs.existsSync(id.safePath(fresh, name)));
}
function consumer(file) {
  const run = readRun(file);
  const build = buildEvidence.verifyBuild(root, 'x64', 'Release', true);
  const directory = path.join(path.dirname(file), 'consumer');
  const exe = id.safePath(directory, 'Release/soui_consumer.exe');
  const linkedLibrary = fs.readFileSync(id.safePath(directory, 'consumer-library-Release.txt'), 'utf8').trim();
  const expectedLibrary = id.safePath(root, 'out/install/x64/lib/Release/soui_sdk.lib');
  requireTrue(path.resolve(linkedLibrary).toLowerCase() === expectedLibrary.toLowerCase() &&
    id.hash(fs.readFileSync(expectedLibrary)) === build.library.files[0].sha256,
    'Consumer did not link the current installed SDK library');
  const result = cp.spawnSync(exe, [], {cwd: path.dirname(exe), encoding: 'utf8', windowsHide: true, timeout: 10000});
  requireTrue(!result.error && result.status === 0, 'Installed SDK CMake consumer failed');
  run.consumer = {passed: true, exitCode: result.status, sdkVersion: run.version, buildId: build.buildId,
    sourceSha256: build.source.sha256, executable: path.relative(root, exe).replace(/\\/g, '/'),
    executableSha256: id.hash(fs.readFileSync(exe)), sdkLibrarySha256: build.library.sha256,
    installedLibrarySha256: id.hash(fs.readFileSync(expectedLibrary))};
  save(file, run);
  return run.consumer;
}
function verifyConsumer(source, receipt) {
  const build = buildEvidence.verifyBuild(source, 'x64', 'Release', true);
  requireTrue(receipt?.passed === true && receipt.exitCode === 0 && receipt.sdkVersion === build.version &&
    receipt.buildId === build.buildId && receipt.sourceSha256 === build.source.sha256 &&
    receipt.sdkLibrarySha256 === build.library.sha256 && typeof receipt.executable === 'string' &&
    receipt.executable.startsWith('out/release-validation/'), 'Consumer build identity differs');
  const exe = id.safePath(source, receipt.executable);
  requireTrue(id.hash(fs.readFileSync(exe)) === receipt.executableSha256 &&
    id.hash(fs.readFileSync(id.safePath(source, 'out/install/x64/lib/Release/soui_sdk.lib'))) === receipt.installedLibrarySha256 &&
    receipt.installedLibrarySha256 === build.library.files[0].sha256, 'Consumer executable or linked library changed');
  return exe;
}
function validateReports(reports, expected) {
  const [generated, fresh, relocated, smoke] = reports;
  const schemas = ['soui.generated-app-validation/v2', 'soui.fresh-app-validation/v2', 'soui.relocation/v2', 'soui.native-smoke/v2'];
  for (let i = 0; i < reports.length; ++i) {
    const report = reports[i];
    requireTrue(report?.schema === schemas[i] && report.passed === true && report.sdkVersion === expected.version &&
      report.rootSourceSha256 === expected.sourceSha256 && report.sdkManifestSha256 === expected.sdkManifestSha256 &&
      report.architecture === 'x64' && report.configuration === 'Release' && !!report.finishedAt,
      'Independent application evidence does not match the current release');
  }
  validateUpgrade(generated.steps, generated.upgradedFrom, expected.version);
  requireTrue(generated.upgradeTested === true && generated.rollbackTested === true && generated.generatedOutsideDistribution === true,
    'Independent upgrade/rollback evidence is incomplete');
  const a = validateStep(fresh.step, expected.version), b = validateStep(relocated.step, expected.version);
  requireTrue(a.name === 'fresh' && b.name === 'relocation' && a.build.source.sha256 === b.build.source.sha256 &&
    a.businessSha256 === b.businessSha256 && a.build.buildId !== b.build.buildId && relocated.copiedOutsideDistribution === true &&
    relocated.buildAndNativeSmoke === true && smoke.buildId === b.smoke.buildId && smoke.sourceSha256 === b.smoke.sourceSha256,
    'Independent relocation evidence differs');
  const ids = new Set();
  for (const step of [...generated.steps, a, b]) {
    requireTrue(!ids.has(step.build.buildId), 'Independent phases reused a build'); ids.add(step.build.buildId);
    if (step.sdkVersion === expected.version) requireTrue(step.sdkManifestSha256 === expected.sdkManifestSha256,
      'Independent phase used a different SDK package');
  }
}
function complete(file) {
  const run = readRun(file);
  const get = name => run.steps.find(step => step.name === name);
  const fresh = validateStep(get('fresh') || {}, run.version);
  const relocated = validateStep(get('relocation') || {}, run.version);
  const upgrades = ['baseline', 'upgrade', 'rollback', 'reupgrade'].map(get);
  validateUpgrade(upgrades, run.oldVersion, run.version);
  requireTrue(fresh.build.source.sha256 === relocated.build.source.sha256 &&
    fresh.businessSha256 === relocated.businessSha256 && fresh.build.buildId !== relocated.build.buildId,
    'Relocation must independently rebuild the same source');
  verifyConsumer(root, run.consumer);
  // Recheck final project bytes and outcomes before publishing aggregate receipts.
  for (const name of ['fresh', 'reupgrade', 'relocation']) {
    const project = projectFor(run, name), expected = get(name);
    requireTrue(sdk.readLock(project).manifestSha256 === run.sdkManifestSha256 &&
      businessSnapshot(project).sha256 === expected.businessSha256 &&
      buildEvidence.verifySmoke(project, 'x64', 'Release').build.buildId === expected.build.buildId,
      'Project changed after validation');
  }
  const common = {sdkVersion: run.version, sdkManifestSha256: run.sdkManifestSha256,
    rootSourceSha256: run.rootSourceSha256, architecture: 'x64', configuration: 'Release',
    passed: true, releaseReady: false, startedAt: run.startedAt, finishedAt: new Date().toISOString()};
  save(path.join(root, 'verification/generated-project.json'), {...common, schema: 'soui.generated-app-validation/v2',
    generatedOutsideDistribution: true, built: true, upgradedFrom: run.oldVersion,
    upgradeTested: true, rollbackTested: true, businessSha256: run.upgradeBusiness.sha256, steps: upgrades, consumer: run.consumer});
  save(path.join(root, 'verification/fresh-project.json'), {...common, schema: 'soui.fresh-app-validation/v2', step: fresh});
  save(path.join(root, 'verification/relocation.json'), {...common, schema: 'soui.relocation/v2',
    copiedOutsideDistribution: true, buildAndNativeSmoke: true, checkedSourceFiles: relocated.build.source.files.length,
    sourceSha256: relocated.build.source.sha256, step: relocated});
  save(path.join(root, 'verification/relocated-native-smoke.json'), {...relocated.smoke, ...common});
  run.finishedAt = common.finishedAt; run.passed = true;
  save(file, run);
  return {...common, phases: run.steps.length, consumer: run.consumer};
}
module.exports = {externalWorkspace, businessSnapshot, validateStep, validateUpgrade, validateReports, verifyConsumer};
if (require.main === module) {
  try {
    const [command, ...args] = process.argv.slice(2);
    const surface = {begin: [4, begin], business: [1, setBusiness], record: [2, record],
      'copy-files': [1, copyFiles], consumer: [1, consumer], complete: [1, complete]};
    requireTrue(surface[command] && args.length === surface[command][0], 'Usage: release-verification.cjs begin RUN WORKSPACE PACKAGE OLD_PACKAGE | business RUN | record RUN PHASE | copy-files RUN | consumer RUN | complete RUN');
    process.stdout.write(id.encoded(surface[command][1](...args)));
  } catch (error) { process.stderr.write(`Release verification: ${error.message}\n`); process.exitCode = 1; }
}
