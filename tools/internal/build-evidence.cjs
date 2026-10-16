'use strict';
const versioning = require('./sdk-version.cjs');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const id = require('./delivery-identity.cjs');

// PowerShell may decode native stdout using the active Windows code page.
// Keep this machine JSON ASCII-only so file names survive that boundary.
function consoleJson(value) {
  return id.encoded(value).replace(/[\u0080-\uffff]/g, character =>
    '\\u' + character.charCodeAt(0).toString(16).padStart(4, '0'));
}

function version(root) {
  const lockFile = id.safePath(root, 'sdk-lock.json');
  const value = fs.existsSync(lockFile) ? id.readJson(lockFile).manifest.version : fs.readFileSync(id.safePath(root, 'VERSION'), 'utf8').trim();
  if (!versioning.isVersion(value)) throw new Error('Invalid SDK VERSION');
  const header = fs.readFileSync(id.safePath(root, 'sdk/include/soui/version.h'), 'utf8');
  const web = id.readJson(id.safePath(root, 'sdk/web/package.json'));
  const lock = id.readJson(id.safePath(root, 'sdk/web/package-lock.json'));
  if (!header.includes(`#define SOUI_SDK_VERSION_STRING "${value}"`) || web.version !== value ||
      lock.version !== value || lock.packages[''].version !== value) throw new Error('SDK version entries differ from VERSION');
  return value;
}
function targets(root) {
  const file = id.safePath(root, 'project.json');
  const names = fs.existsSync(file) ? [id.readJson(file).nativeTarget] : ['SouiStarter', 'SouiWorkbench'];
  if (names.some(name => typeof name !== 'string' || !/^[A-Za-z][A-Za-z0-9_]{1,63}$/.test(name))) throw new Error('Invalid native target');
  return names;
}
function backend(root) {
  const file = id.safePath(root, 'project.json');
  const profile = fs.existsSync(file) ? id.readJson(file).runtimeProfile : undefined;
  if (profile !== undefined && !['compatibility', 'modern'].includes(profile)) throw new Error('Invalid runtime profile');
  return profile === 'modern' ? 'webview2' : 'miniblink';
}
function output(root, arch, config, sdkOnly = false) {
  if (!['x86', 'x64'].includes(arch) || !['Debug', 'Release'].includes(config)) throw new Error('Invalid build architecture/configuration');
  return id.safePath(root, `out/${sdkOnly ? 'sdk-' : ''}${arch}`);
}
function optionalLibraries(root, arch, config, sdkOnly) {
  const dir = output(root, arch, config, sdkOnly), cache = path.join(dir, 'CMakeCache.txt');
  if (!fs.existsSync(cache)) return {};
  const text = fs.readFileSync(cache, 'utf8'), prefix = sdkOnly ? '' : 'sdk/', libraries = {};
  if (/^SOUI_BUILD_WEBVIEW2:BOOL=ON\r?$/m.test(text))
    libraries.webview2 = id.snapshot(dir, [prefix+'renderers/webview2/'+config+'/soui_webview2.lib']);
  if (/^SOUI_BUILD_UIA:BOOL=ON\r?$/m.test(text))
    libraries.uia = id.snapshot(dir, [prefix+'accessibility/uia/'+config+'/soui_uia.lib']);
  return libraries;
}
function profileRequest(root, arch, config, sdkOnly, selected = {}) {
  const profile=selected.nativeToolchain||'MSVC143';
  if(profile==='MSVC143') {if(selected.profileRequest!==undefined)throw new Error('LLVM request requires explicit LLVM22MSVC142');return null;}
  if(profile!=='LLVM22MSVC142')throw new Error('Invalid build toolchain profile');
  const expected=id.safePath(output(root,arch,config,sdkOnly),`llvm-profile-request-${config}.json`);
  if(typeof selected.profileRequest!=='string'||path.resolve(selected.profileRequest)!==expected)throw new Error('LLVM request must belong to this current build/configuration');
  const bytes=fs.readFileSync(expected),request=id.readJson(expected),d=request.discovery;
  if(request.schema!=='soui.build-toolchain-request/v1'||request.profile!==profile||request.architecture!==arch||
      d?.Profile!==profile||d.Architecture!==arch||d.ActualCompilerVersion!==null||d.GeneratorValidated!==false||d.Qualified!==false||
      !['soui.llvm-profile-inputs/v2','soui.llvm-profile-inputs/v3'].includes(d.InputIdentitySchema)||!/^[a-f0-9]{64}$/.test(d.InputIdentity||'')||!/^[a-f0-9]{64}$/.test(d.BridgeIdentity||'')||
      Object.keys(d.ToolPins||{}).join(',')!=='CLANG,LINKER,ARCHIVER,RC,MT,NINJA,CMD,POWERSHELL'||
      d.RequiredInputPins?.length!==(d.InputIdentitySchema==='soui.llvm-profile-inputs/v3'?18:17)||d.BridgePins?.length!==2||Object.keys(d.CMakeDefinitions||{}).length!==12)
    throw new Error('Incomplete filesystem discovery request; no compiler observation is accepted here');
  const toolchain=id.safePath(root,'cmake/toolchains/SouiLLVM22.cmake'),toolchainBytes=fs.readFileSync(toolchain);
  if(path.resolve(request.toolchain?.path||'')!==toolchain||request.toolchain.bytes!==toolchainBytes.length||
      request.toolchain.sha256!==crypto.createHash('sha256').update(toolchainBytes).digest('hex'))throw new Error('Requested product toolchain changed');
  return {profile,request,requestFile:{path:expected,bytes:bytes.length,sha256:crypto.createHash('sha256').update(bytes).digest('hex')},
    observed:null,compilerVerified:false,nativeClosureVerified:false};
}
function llvmConfigurationFiles(dir) {
  const names=['CMakeCache.txt'];
  for(const name of id.walk(dir,'CMakeFiles'))if(/(?:^|\/)CMake(?:C|CXX)Compiler\.cmake$/.test(name))names.push(name);
  if(names.length!==3)throw new Error('LLVM requires real C and CXX configuration files');
  return id.snapshot(dir,names);
}
function start(root, arch, config, sdkOnly = false, selected = {}) {
  const dir = output(root, arch, config, sdkOnly);
  fs.mkdirSync(dir, {recursive: true});
  const reportFile = path.join(dir, `build-${config}.json`);
  // Invalidate before fingerprinting: a failed build must never leave an old success available.
  fs.writeFileSync(reportFile, id.encoded({schema: 'soui.source-build/v2', nativeBuild: 'pending', releaseReady: false}));
  const report = {schema: 'soui.source-build/v2', buildId: crypto.randomUUID(), version: version(root),
    architecture: arch, configuration: config, sdkOnly, nativeBuild: 'pending', releaseReady: false,
    startedAt: new Date().toISOString(), nodeVersion: process.version, source: id.source(root)};
  const requested=profileRequest(root,arch,config,sdkOnly,selected);
  if(requested)report.nativeToolchain=requested;
  fs.writeFileSync(reportFile, id.encoded(report));
  return {buildId: report.buildId, sourceSha256: report.source.sha256};
}
function finish(root, arch, config, sdkOnly = false, selected = {}) {
  const dir = output(root, arch, config, sdkOnly);
  const file = path.join(dir, `build-${config}.json`);
  const report = id.readJson(file);
  if (report.schema !== 'soui.source-build/v2' || report.nativeBuild !== 'pending' || !report.buildId ||
      report.source.sha256 !== id.source(root).sha256) throw new Error('Source changed during build; rebuild the current inputs');
  report.payloads = sdkOnly ? {} : Object.fromEntries(targets(root).map(target =>
    [target, id.payload(path.join(dir, 'bin', config, target), target, backend(root))]));
  if (sdkOnly) report.library = id.snapshot(dir, [`${config}/soui_sdk.lib`]);
  report.optionalLibraries = optionalLibraries(root, arch, config, sdkOnly);
  if (!sdkOnly && !fs.existsSync(path.join(root, 'project.json')) && report.optionalLibraries.webview2)
    report.optionalPayloads = {SouiModernStarter: id.payload(path.join(dir, 'bin', config, 'SouiModernStarter'), 'SouiModernStarter', 'webview2')};
  const requested=profileRequest(root,arch,config,sdkOnly,selected);
  if(JSON.stringify(requested)!==JSON.stringify(report.nativeToolchain||null))throw new Error('Requested toolchain changed during build');
  if(requested)report.nativeToolchain.observed={schema:'soui.llvm-config-files/v1',
    files:llvmConfigurationFiles(dir),compilerVerified:false,scope:'raw CMake configuration files only; actual compile/link observation pending'};
  report.nativeBuild = 'passed';
  report.finishedAt = new Date().toISOString();
  fs.writeFileSync(file, id.encoded(report));
  return {buildId: report.buildId, sourceSha256: report.source.sha256, targets: Object.keys(report.payloads)};
}
function webFiles(root, sdkOnly) {
  const roots = ['sdk/web/dist', ...(sdkOnly ? [] : fs.existsSync(path.join(root, 'project.json')) ?
    ['web/dist'] : ['templates/desktop-app/web/dist', 'templates/modern-app/web/dist', 'demos/workbench/web/dist'])];
  return roots.flatMap(name => id.walk(root, name));
}
function webEvidence(root, arch, config, sdkOnly, record) {
  const dir = output(root, arch, config, sdkOnly);
  const file = id.safePath(root, `out/web-${sdkOnly ? 'sdk' : 'applications'}.json`);
  const current = {schema: 'soui.web-build/v1', sourceSha256: id.source(root).sha256,
    artifacts: id.snapshot(root, webFiles(root, sdkOnly))};
  if (record) {
    const started = id.readJson(path.join(dir, `build-${config}.json`));
    if (started.nativeBuild !== 'pending' || started.source.sha256 !== current.sourceSha256) throw new Error('Source changed during Web build');
    fs.writeFileSync(file, id.encoded(current));
  } else {
    if (!fs.existsSync(file) || JSON.stringify(id.readJson(file)) !== JSON.stringify(current)) throw new Error('SkipWeb requires matching Web source and output evidence; build without -SkipWeb');
  }
  return {webVerified: true, artifactsSha256: current.artifacts.sha256};
}
function verifyBuild(root, arch, config, sdkOnly = false, selected = {}) {
  const dir = output(root, arch, config, sdkOnly);
  const report = id.readJson(path.join(dir, `build-${config}.json`));
  if (report.schema !== 'soui.source-build/v2' || report.nativeBuild !== 'passed' || !report.buildId ||
      report.version !== version(root) || report.architecture !== arch || report.configuration !== config || report.sdkOnly !== sdkOnly ||
      !report.source || report.source.sha256 !== id.source(root).sha256) throw new Error('Build evidence is stale or incomplete; rebuild current source');
  const actualProfile=report.nativeToolchain?.profile||'MSVC143',expectedProfile=selected.nativeToolchain||'MSVC143';
  if(actualProfile!==expectedProfile)throw new Error('Build evidence toolchain profile differs; no cross-profile result reuse');
  if(actualProfile==='LLVM22MSVC142') {
    const requested=profileRequest(root,arch,config,sdkOnly,selected);
    if(JSON.stringify(requested.requestFile)!==JSON.stringify(report.nativeToolchain.requestFile)||
        JSON.stringify(llvmConfigurationFiles(dir))!==JSON.stringify(report.nativeToolchain.observed?.files))throw new Error('LLVM request/configuration files drift');
  }
  if (!sdkOnly) {
    if (JSON.stringify(Object.keys(report.payloads || {}).sort()) !== JSON.stringify(targets(root).sort())) throw new Error('Build payload targets missing');
    for (const target of targets(root)) id.verifyPayload(path.join(dir, 'bin', config, target), target, report.payloads[target], backend(root));
  } else if (!report.library || id.snapshot(dir, [`${config}/soui_sdk.lib`]).sha256 !== report.library.sha256) throw new Error('SDK library drift');
  if (JSON.stringify(report.optionalLibraries || {}) !== JSON.stringify(optionalLibraries(root, arch, config, sdkOnly)))
    throw new Error('Optional SDK library or component configuration drift');
  if (!sdkOnly && !fs.existsSync(path.join(root, 'project.json')) && report.optionalLibraries?.webview2)
    id.verifyPayload(path.join(dir, 'bin', config, 'SouiModernStarter'), 'SouiModernStarter', report.optionalPayloads?.SouiModernStarter, 'webview2');
  return report;
}
function scenarios(root) {
  return targets(root).flatMap(target => target === 'SouiWorkbench' ?
    [{target, scenario: 'default'}, {target, scenario: 'react'}] : [{target, scenario: 'default'}]);
}
function verifyAcceptedBuild(root, arch, config, sdkOnly = false, selected = {}) {
  const current = id.readJson(path.join(output(root, arch, config, sdkOnly), `build-${config}.json`));
  // Selection comes from this current build. It remains a request until the
  // independent standard-build observation and original whole Job are checked.
  const inferred = current.nativeToolchain?.profile === 'LLVM22MSVC142' ?
    {nativeToolchain: 'LLVM22MSVC142', profileRequest: current.nativeToolchain.requestFile?.path} : {};
  const effective = Object.keys(selected).length ? selected : inferred;
  const build = verifyBuild(root, arch, config, sdkOnly, effective);
  if (build.nativeToolchain?.profile === 'LLVM22MSVC142')
    require('./llvm-build-observer.cjs').verifyStandardBuild(root, arch, config, sdkOnly, effective);
  return {build, selected: effective};
}
function beginSmoke(root, arch, config, selected = {}) {
  const dir = output(root, arch, config);
  fs.writeFileSync(path.join(dir, `smoke-${config}.json`), id.encoded({schema: 'soui.native-smoke/v2', passed: false, releaseReady: false}));
  const {build} = verifyAcceptedBuild(root, arch, config, false, selected);
  return {schema: 'soui.native-smoke/v2', buildId: build.buildId, sourceSha256: build.source.sha256,
    version: build.version, architecture: arch, configuration: config, releaseReady: false,
    startedAt: new Date().toISOString(), scenarios: scenarios(root)};
}
function validateRuns(root, report, build, selected = {}) {
  if (report.schema !== 'soui.native-smoke/v2' || report.buildId !== build.buildId || report.version !== build.version ||
      report.sourceSha256 !== build.source.sha256 || report.architecture !== build.architecture ||
      report.configuration !== build.configuration || !Array.isArray(report.runs) ||
      report.runs.length !== scenarios(root).length) throw new Error('Smoke evidence is stale or incomplete');
  for (const expected of scenarios(root)) {
    const runs = report.runs.filter(run => run.target === expected.target && run.scenario === expected.scenario);
    if (runs.length !== 1 || runs[0].passed !== true || runs[0].exitCode !== 0 || runs[0].timedOut !== false ||
        !runs[0].log.includes('application-ready') || !runs[0].log.includes('host-close-complete')) {
      throw new Error(`Smoke scenario failed or missing: ${expected.target}/${expected.scenario}`);
    }
  }
  // Historical v2 receipts retain their original meaning. New Owned receipts
  // additionally bind each real application Job, argv and fresh ready/close log.
  if (report.ownedSupervision !== undefined)
    require('./desktop-smoke.cjs').validateReport(root, report, build, selected);
}
function finishSmoke(root, arch, config, selected = {}) {
  const dir = output(root, arch, config);
  const file = path.join(dir, `smoke-${config}.json`);
  const report = id.readJson(file);
  const verified = verifyAcceptedBuild(root, arch, config, false, selected);
  const {build} = verified;
  validateRuns(root, report, build, verified.selected);
  report.payloads = build.payloads;
  report.passed = true;
  report.finishedAt = new Date().toISOString();
  fs.writeFileSync(file, id.encoded(report));
  return {passed: true, buildId: build.buildId, scenarios: report.runs.length, releaseReady: false};
}
function verifySmoke(root, arch, config, selected = {}) {
  const dir = output(root, arch, config);
  const verified = verifyAcceptedBuild(root, arch, config, false, selected);
  const {build} = verified;
  const smoke = id.readJson(path.join(dir, `smoke-${config}.json`));
  validateRuns(root, smoke, build, verified.selected);
  if (smoke.passed !== true || !smoke.finishedAt || JSON.stringify(smoke.payloads) !== JSON.stringify(build.payloads)) throw new Error('Smoke payload identity missing');
  return {build, smoke};
}
function validateModernSmoke(build, smoke, target, payload, options = {}) {
  if (smoke?.schema !== 'soui.modern-smoke/v2')
    throw new Error('Modern legacy PID-only receipts are not accepted');
  if (!build.optionalLibraries?.webview2 || !payload || typeof options.root !== 'string' || !options.root ||
      smoke.version !== build.version || smoke.buildId !== build.buildId || smoke.sourceSha256 !== build.source.sha256 ||
      smoke.architecture !== build.architecture || smoke.configuration !== build.configuration || smoke.target !== target ||
      smoke.passed !== true || smoke.exitCode !== 0 || smoke.timedOut !== false || !smoke.finishedAt ||
      !Number.isSafeInteger(smoke.pid) || smoke.pid <= 0 || JSON.stringify(smoke.payload) !== JSON.stringify(payload) ||
      typeof smoke.creationFileTime !== 'string' || !/^[1-9]\d{0,19}$/.test(smoke.creationFileTime) ||
      BigInt(smoke.creationFileTime) > 18446744073709551615n ||
      typeof smoke.log !== 'string' || !['application-ready', 'host-close-complete', 'host-started;runtime='].every(text => smoke.log.includes(text)))
    throw new Error('Modern native PID/FILETIME, selected build or lifecycle evidence is incomplete');
  return require('./modern-smoke.cjs').validateReport(options.root, smoke, build, payload,
    options.selected || {}, options.toolsRoot || options.root);
}
function verifyModernSmoke(root, arch, config, selected = {}, toolsRoot = root) {
  const verified = verifyAcceptedBuild(root, arch, config, false, selected);
  const {build} = verified;
  const generated = fs.existsSync(path.join(root, 'project.json'));
  if (generated && backend(root) !== 'webview2') throw new Error('Not a modern application');
  const target = generated ? targets(root)[0] : 'SouiModernStarter';
  const payload = generated ? build.payloads[target] : build.optionalPayloads?.[target];
  const smoke = id.readJson(path.join(output(root, arch, config), `modern-smoke-${config}.json`));
  validateModernSmoke(build, smoke, target, payload, {root, selected: verified.selected, toolsRoot});
  return {build, smoke};
}
function stageApp(root, arch, config, target, destination, selected = {}) {
  const generated = fs.existsSync(id.safePath(root, 'project.json'));
  const optionalModern = !generated && target === 'SouiModernStarter';
  if (!targets(root).includes(target) && !optionalModern) throw new Error('Unknown application target');
  const modern = optionalModern || backend(root) === 'webview2';
  const {build} = modern ? verifyModernSmoke(root, arch, config, selected) : verifySmoke(root, arch, config, selected);
  const expected = optionalModern ? build.optionalPayloads[target] : build.payloads[target];
  destination = id.safePath(destination);
  if (fs.existsSync(destination)) throw new Error('Application stage already exists');
  const app = path.join(output(root, arch, config), 'bin', config, target);
  for (const entry of expected.files) {
    const to = id.safePath(destination, entry.path);
    fs.mkdirSync(path.dirname(to), {recursive: true});
    fs.copyFileSync(id.safePath(app, entry.path), to);
  }
  const payload = id.verifyPayload(destination, target, expected, modern ? 'webview2' : 'miniblink');
  const categories = {executable: 0, kernel: 0, libraries: 0, web: 0};
  for (const file of payload.files) categories[file.path.startsWith('resources/') ? 'web' : file.path === 'node.dll' ? 'kernel' : file.path === `${target}.exe` ? 'executable' : 'libraries'] += file.bytes;
  return {schema: 'soui.application-size/v1', version: build.version, target, architecture: arch, configuration: config,
    buildId: build.buildId, sourceSha256: build.source.sha256, payload, categories, releaseReady: false};
}
module.exports = {version, targets, start, finish, verifyBuild, verifyAcceptedBuild, beginSmoke, validateRuns, finishSmoke, verifySmoke, validateModernSmoke, verifyModernSmoke, stageApp, consoleJson};
if (require.main === module) {
  try {
    const [command, arch, config, ...args] = process.argv.slice(2);
    const root = path.resolve(__dirname, '../..');
    const commands = {start, finish, 'verify-build': verifyBuild, 'begin-smoke': beginSmoke, 'finish-smoke': finishSmoke,
      'verify-smoke': verifySmoke, 'verify-modern-smoke': verifyModernSmoke,
      'record-web': (...a) => webEvidence(...a, true), 'verify-web': (...a) => webEvidence(...a, false)};
    let result;
    if (commands[command] || command === 'stage-app') {
      const remaining=[...args],sdkOnly=remaining[0]==='sdk-only';if(sdkOnly)remaining.shift();
      const stageArguments = command === 'stage-app' ? remaining.splice(0, 2) : [];
      if(command==='stage-app' && (sdkOnly || stageArguments.length!==2))throw new Error('Stage requires target and destination');
      const selected={},seen=new Set();
      while(remaining.length){const flag=remaining.shift(),key={'--native-toolchain':'nativeToolchain','--profile-request':'profileRequest'}[flag];
        if(!key||seen.has(key)||remaining.length===0)throw new Error('Invalid build-evidence profile arguments');seen.add(key);selected[key]=remaining.shift();}
      if(Object.keys(selected).length&&(!selected.nativeToolchain||!selected.profileRequest))throw new Error('Both LLVM evidence arguments are required');
      if(command==='stage-app')result=stageApp(root,arch,config,...stageArguments,selected);
      else if(['record-web','verify-web'].includes(command)){if(Object.keys(selected).length)throw new Error('Web evidence does not select a native profile');result=webEvidence(root,arch,config,sdkOnly,command==='record-web');}
      else if(command==='verify-build')result=verifyAcceptedBuild(root,arch,config,sdkOnly,selected).build;
      else if(['start','finish'].includes(command))result=commands[command](root,arch,config,sdkOnly,selected);
      else {if(sdkOnly)throw new Error('Smoke requires application build');result=commands[command](root,arch,config,selected);}
    }
    else throw new Error('Invalid build-evidence command/arguments');
    console.log(consoleJson(result));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
