'use strict';
const fs = require('node:fs');
const path = require('node:path');
const cp = require('node:child_process');
const id = require('./delivery-identity.cjs');
const buildEvidence = require('./build-evidence.cjs');
const sdk = require('../sdk/soui-sdk.cjs');
const crypto = require('node:crypto');
const desktopSmoke = require('./desktop-smoke.cjs');
const {launchOwned} = require('./owned-process.cjs');
const {validateStage} = require('./devkit-command.cjs');
const {inspectTool, verifyTool} = require('./standard-build.cjs');
const root = path.resolve(__dirname, '../..');
const reportNames = ['generated-project.json', 'fresh-project.json', 'relocation.json', 'relocated-native-smoke.json'];
const hashPattern = /^[a-f0-9]{64}$/;
const llvm = require('./llvm-build-observer.cjs');
const installed = require('./llvm-installed-acceptance.cjs');
const {isDeepStrictEqual} = require('node:util');
const phaseNames = ['fresh','baseline','upgrade','rollback','reupgrade','relocation'];
const llvmPathNames = ['LlvmRoot','MsvcRoot','WindowsSdkRoot','NinjaExe','WindowsCmdExe','PowerShellExe'];
function requireTrue(value, message) { if (!value) throw new Error(message); }
function architectureOf(value = 'x64') {
  requireTrue(['x86', 'x64'].includes(value), 'Release architecture must be x86 or x64');
  return value;
}
function reportDirectories(architecture) {
  const directories = [path.join(root, 'verification/release-validation', architectureOf(architecture))];
  // Retain the established x64 collector view; x86 never invalidates or replaces it.
  if (architecture === 'x64') directories.push(path.join(root, 'verification'));
  return directories;
}
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
function validateStep(step, version, architecture = 'x64', nativeToolchain = 'MSVC143') {
  architectureOf(architecture);
  requireTrue(['MSVC143','LLVM22MSVC142'].includes(nativeToolchain),'Explicit release phase profile required');
  const {build, smoke} = step;
  requireTrue(step.architecture === architecture && step.sdkVersion === version && hashPattern.test(step.sdkManifestSha256) &&
    hashPattern.test(step.businessSha256), 'Step SDK or business identity is invalid');
  requireTrue(build?.schema === 'soui.source-build/v2' && build.nativeBuild === 'passed' &&
    build.version === version && build.architecture === architecture && build.configuration === 'Release' &&
    (build.nativeToolchain?.profile || 'MSVC143') === nativeToolchain &&
    build.sdkOnly === false && typeof build.buildId === 'string' && !!build.buildId &&
    hashPattern.test(build.source?.sha256), 'Step build identity is invalid');
  requireTrue(smoke?.schema === 'soui.native-smoke/v2' && smoke.passed === true && !!smoke.finishedAt &&
    smoke.version === version && smoke.architecture === architecture && smoke.configuration === 'Release' &&
    smoke.buildId === build.buildId && smoke.sourceSha256 === build.source.sha256 &&
    smoke.ownedSupervision?.schema === 'soui.native-smoke-owned/v1' &&
    Array.isArray(smoke.runs) && smoke.runs.length === 1, 'Step smoke identity is invalid');
  const run = smoke.runs[0];
  if(nativeToolchain==='LLVM22MSVC142')requireTrue(step.compilerObservation?.compilerVerified===true&&
    step.compilerObservation.buildId===build.buildId&&step.compilerObservation.sourceSha256===build.source.sha256&&
    ['accepted','observation','requestFile'].every(key=>hashPattern.test(step.compilerObservation[key]?.sha256))&&
    smoke.nativeToolchain?.profile===nativeToolchain&&
    isDeepStrictEqual(smoke.nativeToolchain.requestFile,step.compilerObservation.requestFile)&&
    build.nativeToolchain.requestFile?.bytes===step.compilerObservation.requestFile.bytes&&
    build.nativeToolchain.requestFile?.sha256===step.compilerObservation.requestFile.sha256&&
    typeof build.nativeToolchain.requestFile?.path==='string'&&
    path.resolve(build.nativeToolchain.requestFile.path)===path.resolve(step.compilerObservation.requestFile.path)&&
    hashPattern.test(smoke.nativeToolchain.inputIdentity||'')&&hashPattern.test(smoke.nativeToolchain.bridgeIdentity||'')&&
    smoke.nativeToolchain.inputIdentity===build.nativeToolchain.request?.discovery?.InputIdentity&&
    smoke.nativeToolchain.bridgeIdentity===build.nativeToolchain.request?.discovery?.BridgeIdentity,
    'LLVM phase requires its actual separately accepted compiler and smoke request');
  requireTrue(run.passed === true && run.exitCode === 0 && run.timedOut === false &&
    run.scenario === 'default' && typeof run.log === 'string' &&
    run.log.includes('application-ready') && run.log.includes('host-close-complete'), 'Step did not actually become ready and close');
  return step;
}
function validateUpgrade(steps, oldVersion, version, architecture = 'x64', nativeToolchain = 'MSVC143') {
  const names = ['baseline', 'upgrade', 'rollback', 'reupgrade'];
  requireTrue(typeof oldVersion === 'string' && oldVersion !== version, 'Upgrade must use a distinct prior version');
  requireTrue(Array.isArray(steps) && steps.length === names.length, 'Upgrade/rollback phases are missing');
  const ids = new Set();
  for (let index = 0; index < names.length; ++index) {
    const step = validateStep(steps[index], index % 2 ? version : oldVersion, architecture,
      index%2?nativeToolchain:'MSVC143');
    requireTrue(step.name === names[index] && step.businessSha256 === steps[0].businessSha256,
      'Upgrade order or preserved business identity differs');
    requireTrue(!ids.has(step.build.buildId), 'Upgrade phases reused a build');
    ids.add(step.build.buildId);
  }
  requireTrue(steps[0].sdkManifestSha256 === steps[2].sdkManifestSha256 &&
    steps[1].sdkManifestSha256 === steps[3].sdkManifestSha256, 'Upgrade/rollback package identity differs');
}
function save(file, value) {
  file = id.safePath(file); fs.mkdirSync(path.dirname(file), {recursive: true});
  fs.writeFileSync(file, id.encoded(value));
}
function create(file, value) { fs.writeFileSync(id.safePath(file), id.encoded(value), {flag: 'wx'}); }
function validateReleaseProfile(value) {
  requireTrue(value?.schema==='soui.release-requested-profile/v1'&&value.profile==='LLVM22MSVC142'&&
    typeof value.toolsRoot==='string'&&path.isAbsolute(value.toolsRoot)&&
    id.safePath(value.toolsRoot).toLowerCase()!==root.toLowerCase()&&value.legacyToolsRoot===root&&
    isDeepStrictEqual(Object.keys(value.paths||{}).sort(),[...llvmPathNames].sort())&&
    Object.values(value.paths).every(item=>typeof item==='string'&&/^[A-Za-z]:[\\/]/.test(item)&&!/[;"\x00-\x1f]/.test(item))&&
    value.qualificationPassed===false&&value.releaseReady===false,'Explicit independent LLVM stock and six safe inputs required');
  return value;
}
function phaseProfile(run,name) {
  requireTrue(phaseNames.includes(name),'Unknown release phase');
  return run.nativeToolchain&&!['baseline','rollback'].includes(name)?'LLVM22MSVC142':'MSVC143';
}
function selectedFor(run,name) {
  return phaseProfile(run,name)==='LLVM22MSVC142'?{nativeToolchain:'LLVM22MSVC142',
    profileRequest:llvm.profileRequestPath(projectFor(run,name),{architecture:run.architecture,configuration:'Release',sdkOnly:false})}:{};
}
function compilerBinding(project,architecture,sdkOnly=false) {
  const requestFile=llvm.profileRequestPath(project,{architecture,configuration:'Release',sdkOnly});
  const actual=llvm.verifyStandardBuild(project,architecture,'Release',sdkOnly,{nativeToolchain:'LLVM22MSVC142',profileRequest:requestFile});
  const acceptedFile=path.join(project,'out',(sdkOnly?'sdk-':'')+architecture,'standard-llvm-Release.json');
  const acceptedBytes=fs.readFileSync(acceptedFile),accepted={path:acceptedFile,bytes:acceptedBytes.length,sha256:id.hash(acceptedBytes)};
  return {compilerVerified:true,buildId:actual.build.buildId,sourceSha256:actual.build.source.sha256,
    accepted,observation:actual.accepted.observation,requestFile:require('./llvm-profile-inputs.cjs').readRequest(requestFile,architecture,project).pin,
    validationTools:actual.accepted.validationTools};
}
function readRun(file) {
  file = id.safePath(file);
  requireTrue(inside(file, path.join(root, 'out/release-validation')), 'Run file must be in release validation output');
  const run = id.readJson(file);
  if(run.nativeToolchain){validateReleaseProfile(run.nativeToolchain.requested);
    const row=run.nativeToolchain.input,bytes=fs.readFileSync(id.safePath(row.path));
    requireTrue(row.path===path.join(path.dirname(file),'requested-profile.json')&&row.bytes===bytes.length&&
      row.sha256===id.hash(bytes)&&isDeepStrictEqual(JSON.parse(bytes.toString('utf8')),run.nativeToolchain.requested),
      'Requested release profile changed');}
  requireTrue(run.schema === 'soui.release-validation-run/v2' && run.root === root &&
    inside(file, path.join(root, 'out/release-validation', architectureOf(run.architecture))) &&
    run.version === buildEvidence.version(root) && run.rootSourceSha256 === id.source(root).sha256,
    'Release source changed during verification');
  const current = sdk.readPackage(run.package);
  const previous = sdk.readPackage(run.oldPackage);
  requireTrue(current.version === run.version && previous.version === run.oldVersion &&
    id.hash(id.encoded(current)) === run.sdkManifestSha256 &&
    id.hash(id.encoded(previous)) === run.oldSdkManifestSha256, 'Fixed SDK package changed during verification');
  return run;
}
function begin(file, workspace, packagePath, oldPackage, architecture = 'x64', profileFile) {
  architecture = architectureOf(architecture);
  file = id.safePath(file);
  requireTrue(inside(file, path.join(root, 'out/release-validation', architecture)) && !fs.existsSync(file),
    'Use a new run file in this architecture release validation output');
  const version = buildEvidence.version(root);
  fs.mkdirSync(path.dirname(file), {recursive: true});
  // Invalidate before package/path validation or directory creation can fail.
  for (const [index, directory] of reportDirectories(architecture).entries()) for (const name of reportNames) {
    const destination = path.join(directory, name);
    if (fs.existsSync(destination)) {
      const previous = path.join(path.dirname(file), 'previous-reports', String(index), name);
      fs.mkdirSync(path.dirname(previous), {recursive: true});
      fs.copyFileSync(destination, previous, fs.constants.COPYFILE_EXCL);
    }
    save(destination, {schema: 'soui.release-validation-pending/v2', sdkVersion: version, architecture,
      passed: false, releaseReady: false, startedAt: new Date().toISOString()});
  }
  const current = sdk.readPackage(packagePath);
  const previous = sdk.readPackage(oldPackage);
  requireTrue(current.version === version && current.version !== previous.version, 'Use current and different prior SDK versions');
  const sourceCheck = cp.spawnSync(process.execPath, [path.join(root, 'tools/sdk/soui-sdk.cjs'),
    'verify-source', '--package', packagePath, '--source', root], {encoding: 'utf8', windowsHide: true});
  requireTrue(sourceCheck.status === 0, 'Current SDK package differs from source: ' + (sourceCheck.stderr || ''));
  const external = externalWorkspace(root, workspace);
  const run = {schema: 'soui.release-validation-run/v2', root, architecture, version, oldVersion: previous.version,
    rootSourceSha256: id.source(root).sha256, package: id.safePath(packagePath), oldPackage: id.safePath(oldPackage),
    sdkManifestSha256: id.hash(id.encoded(current)), oldSdkManifestSha256: id.hash(id.encoded(previous)),
    workspace: external, startedAt: new Date().toISOString(), steps: [], releaseReady: false};
  if(profileFile){requireTrue(id.safePath(profileFile)===path.join(path.dirname(file),'requested-profile.json'),'Fixed requested profile input required');
    const bytes=fs.readFileSync(profileFile),requested=validateReleaseProfile(JSON.parse(bytes.toString('utf8')));
    requireTrue(fs.realpathSync.native(requested.toolsRoot).toLowerCase()!==fs.realpathSync.native(root).toLowerCase(),
      'LLVM and historical MSVC stock must have physically distinct source owners');
    run.nativeToolchain={requested,input:{path:id.safePath(profileFile),bytes:bytes.length,sha256:id.hash(bytes)}};
    run.phaseSources={};}
  fs.mkdirSync(path.dirname(id.safePath(file)), {recursive: true});
  save(file, run);
  fs.mkdirSync(external);
  return {version, oldVersion: run.oldVersion, architecture, workspace: external};
}
function projectFor(run, name) {
  if(run.nativeToolchain){requireTrue(phaseNames.includes(name),'Unknown release phase');return id.safePath(run.workspace,'phases/'+name);}
  const directory = name === 'fresh' ? 'fresh' : name === 'relocation' ? 'relocated' : 'upgrade';
  return id.safePath(run.workspace, directory);
}
function transactionProject(run,name){return id.safePath(run.workspace,name==='fresh'?'fresh':name==='relocation'?'relocated':'upgrade');}
function preparePhase(file,name){
  const run=readRun(file);requireTrue(run.nativeToolchain&&phaseNames.includes(name)&&!run.phaseSources[name],'Fresh explicit LLVM release source phase required');
  const source=transactionProject(run,name),destination=projectFor(run,name),lock=sdk.readLock(source);
  requireTrue(!fs.existsSync(destination),'Phase Source/output cannot be reused');
  const before=id.source(source),business=businessSnapshot(source),names=new Set([...before.files.map(row=>row.path),
    ...lock.manifest.files.map(row=>row.path),'.gitattributes','.gitignore','sdk-lock.json']);
  fs.mkdirSync(destination,{recursive:true});
  for(const relative of [...names].sort()){
    requireTrue(!relative.split('/').some(part=>['out','.git','.vs','node_modules'].includes(part))&&!/\.(obj|pch)$/i.test(relative),
      'Phase copying cannot reuse build objects, caches or requests');
    const original=id.safePath(source,relative);if(!fs.existsSync(original))continue;
    const copied=id.safePath(destination,relative);fs.mkdirSync(path.dirname(copied),{recursive:true});fs.copyFileSync(original,copied,fs.constants.COPYFILE_EXCL);
  }
  requireTrue(isDeepStrictEqual(id.source(source),before)&&isDeepStrictEqual(id.source(destination),before)&&
    businessSnapshot(destination).sha256===business.sha256&&sdk.readLock(destination).manifestSha256===lock.manifestSha256,
    'Transaction Source/lock/business changed during phase copying');
  run.phaseSources[name]={transaction:source,project:destination,sourceSha256:before.sha256,businessSha256:business.sha256,
    sdkManifestSha256:lock.manifestSha256,nativeToolchain:phaseProfile(run,name)};save(file,run);
  return {project:destination,nativeToolchain:phaseProfile(run,name),toolsRoot:phaseProfile(run,name)==='LLVM22MSVC142'?run.nativeToolchain.requested.toolsRoot:root};
}
function setBusiness(file) {
  const run = readRun(file);
  requireTrue(!run.upgradeBusiness, 'Upgrade business identity was already captured');
  run.upgradeBusiness = businessSnapshot(run.nativeToolchain?transactionProject(run,'baseline'):projectFor(run, 'baseline'));
  save(file, run);
  return {businessSha256: run.upgradeBusiness.sha256, files: run.upgradeBusiness.files.length};
}
async function smoke(file, name, nonce) {
  const run = readRun(file);
  requireTrue(/^[a-f0-9]{32}$/.test(nonce || '') && nonce === process.env.SOUI_RELEASE_DESKTOP_NONCE,
    'Use the actual normal-lease-holding Verify-Release.ps1 entry');
  requireTrue(['fresh', 'baseline', 'upgrade', 'rollback', 'reupgrade', 'relocation'].includes(name) &&
    !run.steps.some(step => step.name === name), 'Unknown or already recorded release phase');
  const receipt = path.join(path.dirname(file), `smoke-${name}.json`);
  requireTrue(!fs.existsSync(receipt), 'Smoke phase already attempted; retain it and use a new release run');
  const project = projectFor(run, name);
  const selected=selectedFor(run,name),profile=phaseProfile(run,name);
  if(profile==='LLVM22MSVC142')compilerBinding(project,run.architecture);
  else buildEvidence.verifyBuild(project, run.architecture, 'Release');
  const result = await desktopSmoke.runSmoke(project, {architecture: run.architecture,
    configuration: 'Release',toolsRoot:profile==='LLVM22MSVC142'?run.nativeToolchain.requested.toolsRoot:root,
    ...(profile==='LLVM22MSVC142'?{toolchainRequest:selected.profileRequest}:{})});
  // runSmoke preserves every application raw Job/finished/log attempt, including failures.
  create(receipt, result);
  requireTrue(result.passed === true, 'Independent application did not close its whole Job naturally');
  return result;
}
function record(file, name) {
  const run = readRun(file);
  requireTrue(['fresh', 'baseline', 'upgrade', 'rollback', 'reupgrade', 'relocation'].includes(name) &&
    !run.steps.some(step => step.name === name), 'Unknown or duplicate release phase');
  const project = projectFor(run, name);
  const lock = sdk.readLock(project);
  const version = ['baseline', 'rollback'].includes(name) ? run.oldVersion : run.version;
  const selected=selectedFor(run,name),{build, smoke} = buildEvidence.verifySmoke(project, run.architecture, 'Release',selected);
  const business = businessSnapshot(project);
  const step = {name, architecture: run.architecture, sdkVersion: lock.manifest.version, sdkManifestSha256: lock.manifestSha256,
    businessSha256: business.sha256,build,smoke,...(phaseProfile(run,name)==='LLVM22MSVC142'?{compilerObservation:compilerBinding(project,run.architecture)}:{})};
  validateStep(step, version, run.architecture,phaseProfile(run,name));
  if(run.nativeToolchain)requireTrue(run.phaseSources[name]?.sourceSha256===build.source.sha256&&
    run.phaseSources[name].businessSha256===business.sha256&&run.phaseSources[name].sdkManifestSha256===lock.manifestSha256,
    'Compiled phase differs from the actual transaction Source state');
  requireTrue(step.sdkManifestSha256 === (version === run.oldVersion ? run.oldSdkManifestSha256 : run.sdkManifestSha256),
    'Project installed an unexpected SDK package');
  if (!['fresh', 'relocation'].includes(name)) requireTrue(run.upgradeBusiness &&
    business.sha256 === run.upgradeBusiness.sha256, 'SDK installation overwrote application business files');
  create(path.join(path.dirname(file), `phase-${name}.json`), step);
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
function prepareConsumer(file){
  const run=readRun(file);requireTrue(run.nativeToolchain,'Explicit LLVM release consumer preparation required');
  const current=installed.select(root,run.architecture,'Release',run.nativeToolchain.requested.toolsRoot),
    directory=path.join(root,'tests/package-consumer'),consumerSource=id.snapshot(directory,id.walk(directory));
  const pending={schema:'soui.release-installed-consumer-pending/v1',architecture:run.architecture,configuration:'Release',
    buildId:current.build.buildId,sourceSha256:current.build.source.sha256,consumerSource,nativeToolchain:current.selection};
  create(path.join(path.dirname(file),'consumer-pending.json'),pending);
  return {inputOnly:true,compilerVerified:false,releaseReady:false};
}
async function consumer(file) {
  const run = readRun(file);
  const native=run.nativeToolchain?installed.select(root,run.architecture,'Release',run.nativeToolchain.requested.toolsRoot):null;
  const build=native?native.build:buildEvidence.verifyBuild(root, run.architecture, 'Release', true);
  if(native)installed.verifyRelease(root,path.dirname(file),native.selection);
  const directory = path.join(path.dirname(file), 'consumer');
  const exe = id.safePath(directory, 'Release/soui_consumer.exe');
  const linkedLibrary = fs.readFileSync(id.safePath(directory, 'consumer-library-Release.txt'), 'utf8').trim();
  const expectedLibrary = id.safePath(root, `out/install/${run.architecture}/lib/Release/soui_sdk.lib`);
  requireTrue(path.resolve(linkedLibrary).toLowerCase() === expectedLibrary.toLowerCase() &&
    id.hash(fs.readFileSync(expectedLibrary)) === build.library.files[0].sha256,
    'Consumer did not link the current installed SDK library');
  const prepared = native?native.prepared.setup:require('./measurement-validation.cjs').prepared(run.architecture);
  const runner = inspectTool(prepared.tools.OwnedProcessRunner), subject = inspectTool(exe);
  const attempt = path.join(path.dirname(file), 'consumer-owned-' + crypto.randomUUID()); fs.mkdirSync(attempt);
  const report = path.join(attempt, 'owned.ndjson'), finishedPath = path.join(attempt, 'finished.json');
  const stdout = fs.openSync(path.join(attempt, 'stdout.raw'), 'wx'), stderr = fs.openSync(path.join(attempt, 'stderr.raw'), 'wx');
  const expected = {executable: exe, cwd: path.dirname(exe), args: [], timeoutMs: 10000, drainMs: 2000,
    executableSha256: subject.sha256, runnerSha256: runner.sha256};
  let active = null, ready = null, finished = null, interrupted = false;
  const stop = () => { interrupted = true; active?.stop(); };
  process.on('SIGINT', stop); process.on('SIGTERM', stop);
  try {
    active = launchOwned(exe, [], {runner: runner.path, cwd: expected.cwd, report,
      timeoutMs: expected.timeoutMs, drainMs: expected.drainMs, stdout, stderr});
    expected.contextSha256 = id.hash(fs.readFileSync(report + '.context.json'));
    try { ready = await active.ready; } catch { /* Await and retain the actual finished failure. */ }
    if (interrupted) active.stop();
    finished = await active.finished; create(finishedPath, finished);
    validateStage(fs.readFileSync(report), fs.readFileSync(report + '.context.json'), ready, finished, expected);
    requireTrue(!interrupted, 'Consumer cancellation cannot be admitted');
    verifyTool(subject); verifyTool(runner);
    if(native)installed.verifyRelease(root,path.dirname(file),native.selection);
    else buildEvidence.verifyBuild(root, run.architecture, 'Release', true);
  } catch (error) {
    create(path.join(attempt, 'failure.json'), {passed: false, failure: error.message, releaseReady: false});
    throw error;
  } finally {
    if (active && !finished) { active.stop(); finished = await active.finished; create(finishedPath, finished); }
    process.off('SIGINT', stop); process.off('SIGTERM', stop); fs.closeSync(stdout); fs.closeSync(stderr);
  }
  const relative = full => path.relative(root, full).replace(/\\/g, '/');
  run.consumer = {passed: true, exitCode: finished.exitCode, architecture: run.architecture,
    sdkVersion: run.version, buildId: build.buildId,
    sourceSha256: build.source.sha256, executable: path.relative(root, exe).replace(/\\/g, '/'),
    executableSha256: id.hash(fs.readFileSync(exe)), sdkLibrarySha256: build.library.sha256,
    installedLibrarySha256: id.hash(fs.readFileSync(expectedLibrary)),releaseReady:false,
    ...(native?{nativeToolchain:{...native.selection,consumer:installed.verifyRelease(root,path.dirname(file),native.selection).consumer}}:{}),
    ownedSupervision: {schema: 'soui.release-consumer-owned/v1', attempt: relative(attempt), runner, ready, expected,
      report: relative(report), context: relative(report + '.context.json'), finished: relative(finishedPath),
      finishedSha256: id.hash(fs.readFileSync(finishedPath)), controllerClosure: 'external-caller-evidence-required'}};
  save(file, run);
  return run.consumer;
}
function verifyConsumer(source, receipt, architecture = 'x64') {
  architecture = architectureOf(architecture);
  const native=receipt?.nativeToolchain?installed.verifyRelease(source,path.dirname(receipt.nativeToolchain.consumer.path),receipt.nativeToolchain):null;
  const build=native?native.build:buildEvidence.verifyBuild(source, architecture, 'Release', true);
  requireTrue(receipt?.passed === true && receipt.exitCode === 0 && receipt.sdkVersion === build.version &&
    receipt.architecture === architecture &&
    receipt.buildId === build.buildId && receipt.sourceSha256 === build.source.sha256 &&
    receipt.sdkLibrarySha256 === build.library.sha256 && typeof receipt.executable === 'string' &&
    receipt.executable.startsWith(`out/release-validation/${architecture}/`), 'Consumer build identity differs');
  const exe = id.safePath(source, receipt.executable);
  if(native)requireTrue(exe===path.join(path.dirname(receipt.nativeToolchain.consumer.path),'consumer','Release','soui_consumer.exe'),
    'Release consumer output differs from its fixed installed compiler evidence');
  requireTrue(id.hash(fs.readFileSync(exe)) === receipt.executableSha256 &&
    id.hash(fs.readFileSync(id.safePath(source, `out/install/${architecture}/lib/Release/soui_sdk.lib`))) === receipt.installedLibrarySha256 &&
    receipt.installedLibrarySha256 === build.library.files[0].sha256, 'Consumer executable or linked library changed');
  const owned = receipt.ownedSupervision;
  requireTrue(owned?.schema === 'soui.release-consumer-owned/v1' && typeof owned.attempt === 'string' &&
    owned.attempt.startsWith(`out/release-validation/${architecture}/`) &&
    /^consumer-owned-[a-f0-9-]{36}$/.test(path.basename(owned.attempt)), 'Consumer Owned attempt differs');
  const rawFile = value => {
    requireTrue(typeof value === 'string' && value.startsWith(owned.attempt + '/'), 'Consumer evidence escaped its attempt');
    return id.safePath(source, value);
  };
  const finishedBytes = fs.readFileSync(rawFile(owned.finished));
  requireTrue(id.hash(finishedBytes) === owned.finishedSha256 && owned.expected.executable === exe &&
    owned.expected.cwd === path.dirname(exe) && JSON.stringify(owned.expected.args) === '[]' &&
    owned.expected.timeoutMs === 10000 && owned.expected.drainMs === 2000 &&
    owned.expected.executableSha256 === receipt.executableSha256 &&
    owned.expected.runnerSha256 === owned.runner.sha256, 'Consumer actual launch identity differs');
  verifyTool(owned.runner);
  if(native)requireTrue(owned.runner.path===installed.runtimeRunner(source,architecture,'Release',receipt.nativeToolchain),
    'Consumer Runner differs from the selected authenticated stock');
  validateStage(fs.readFileSync(rawFile(owned.report)), fs.readFileSync(rawFile(owned.context)),
    owned.ready, JSON.parse(finishedBytes.toString('utf8')), owned.expected);
  return exe;
}
function validateReports(reports, expected) {
  const [generated, fresh, relocated, smoke] = reports;
  const architecture = architectureOf(expected.architecture || 'x64');
  const nativeToolchain=expected.nativeToolchain||'MSVC143';
  const schemas = ['soui.generated-app-validation/v3', 'soui.fresh-app-validation/v3', 'soui.relocation/v3', 'soui.native-smoke/v2'];
  for (let i = 0; i < reports.length; ++i) {
    const report = reports[i];
    requireTrue(report?.schema === schemas[i] && report.passed === true && report.sdkVersion === expected.version &&
      report.rootSourceSha256 === expected.sourceSha256 && report.sdkManifestSha256 === expected.sdkManifestSha256 &&
      report.architecture === architecture && report.configuration === 'Release' && !!report.finishedAt,
      'Independent application evidence does not match the current release');
    if(nativeToolchain==='LLVM22MSVC142')requireTrue(report.nativeToolchain===nativeToolchain&&
      report.formalPassed===false&&report.qualificationPassed===false&&report.releaseReady===false,
      'LLVM aggregate must retain explicit selected profile and unqualified scope');
  }
  validateUpgrade(generated.steps, generated.upgradedFrom, expected.version, architecture,nativeToolchain);
  requireTrue(generated.upgradeTested === true && generated.rollbackTested === true && generated.generatedOutsideDistribution === true,
    'Independent upgrade/rollback evidence is incomplete');
  const a = validateStep(fresh.step, expected.version, architecture,nativeToolchain), b = validateStep(relocated.step, expected.version, architecture,nativeToolchain);
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
  const fresh = validateStep(get('fresh') || {}, run.version, run.architecture,phaseProfile(run,'fresh'));
  const relocated = validateStep(get('relocation') || {}, run.version, run.architecture,phaseProfile(run,'relocation'));
  const upgrades = ['baseline', 'upgrade', 'rollback', 'reupgrade'].map(get);
  validateUpgrade(upgrades, run.oldVersion, run.version, run.architecture,phaseProfile(run,'upgrade'));
  requireTrue(fresh.build.source.sha256 === relocated.build.source.sha256 &&
    fresh.businessSha256 === relocated.businessSha256 && fresh.build.buildId !== relocated.build.buildId,
    'Relocation must independently rebuild the same source');
  verifyConsumer(root, run.consumer, run.architecture);
  for (const step of run.steps){
    const project=projectFor(run,step.name),selected=selectedFor(run,step.name);
    desktopSmoke.validateReport(project,step.smoke,step.build,selected);
    if(phaseProfile(run,step.name)==='LLVM22MSVC142')requireTrue(isDeepStrictEqual(compilerBinding(project,run.architecture),step.compilerObservation),
      'Original phase compiler evidence or independently owned stock changed');
  }
  // Recheck final project bytes and outcomes before publishing aggregate receipts.
  for (const name of ['fresh', 'reupgrade', 'relocation']) {
    const project = projectFor(run, name), expected = get(name);
    requireTrue(sdk.readLock(project).manifestSha256 === run.sdkManifestSha256 &&
      businessSnapshot(project).sha256 === expected.businessSha256 &&
      buildEvidence.verifySmoke(project, run.architecture, 'Release',selectedFor(run,name)).build.buildId === expected.build.buildId,
      'Project changed after validation');
  }
  const common = {sdkVersion: run.version, sdkManifestSha256: run.sdkManifestSha256,
    rootSourceSha256: run.rootSourceSha256, architecture: run.architecture, configuration: 'Release',
    passed: true, formalPassed: false, qualificationPassed: false, releaseReady: false,
    controllerClosure: 'external-caller-evidence-required', startedAt: run.startedAt, finishedAt: new Date().toISOString()};
  if(run.nativeToolchain)common.nativeToolchain='LLVM22MSVC142';
  const reports = {
  'generated-project.json': {...common, schema: 'soui.generated-app-validation/v3',
    generatedOutsideDistribution: true, built: true, upgradedFrom: run.oldVersion,
    upgradeTested: true, rollbackTested: true, businessSha256: run.upgradeBusiness.sha256, steps: upgrades, consumer: run.consumer},
  'fresh-project.json': {...common, schema: 'soui.fresh-app-validation/v3', step: fresh},
  'relocation.json': {...common, schema: 'soui.relocation/v3',
    copiedOutsideDistribution: true, buildAndNativeSmoke: true, checkedSourceFiles: relocated.build.source.files.length,
    sourceSha256: relocated.build.source.sha256, step: relocated},
  'relocated-native-smoke.json': {...relocated.smoke, ...common}};
  const originals = path.join(path.dirname(file), 'reports'); fs.mkdirSync(originals);
  run.receipts = [];
  for (const [name, value] of Object.entries(reports)) {
    const original = path.join(originals, name); create(original, value);
    run.receipts.push({path: original, bytes: fs.statSync(original).size, sha256: id.hash(fs.readFileSync(original))});
    for (const directory of reportDirectories(run.architecture)) save(path.join(directory, name), value);
  }
  run.finishedAt = common.finishedAt; run.passed = true;
  save(file, run);
  return {...common, phases: run.steps.length, consumer: run.consumer, receipts: run.receipts};
}
module.exports = {externalWorkspace, businessSnapshot, validateStep, validateUpgrade, validateReports, verifyConsumer,validateReleaseProfile,phaseProfile};
if (require.main === module) (async () => {
  try {
    const [command, ...args] = process.argv.slice(2);
    const surface = {begin: [5, begin], business: [1, setBusiness], smoke: [3, smoke], record: [2, record],'prepare-phase':[2,preparePhase],'prepare-consumer':[1,prepareConsumer],
      'copy-files': [1, copyFiles], consumer: [1, consumer], complete: [1, complete]};
    requireTrue(surface[command] && (args.length === surface[command][0]||(command==='begin'&&args.length===6)), 'Usage: release-verification.cjs begin RUN WORKSPACE PACKAGE OLD_PACKAGE x86|x64 [REQUESTED_PROFILE] | prepare-phase RUN PHASE | business RUN | smoke RUN PHASE NONCE | record RUN PHASE | copy-files RUN | consumer RUN | complete RUN');
    process.stdout.write(id.encoded(await surface[command][1](...args)));
  } catch (error) { process.stderr.write(`Release verification: ${error.message}\n`); process.exitCode = 1; }
})();
