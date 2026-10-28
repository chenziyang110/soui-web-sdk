'use strict';
const versioning = require('./sdk-version.cjs');
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const id = require('./delivery-identity.cjs');
const webProfiles = require('./web-build-profile.cjs');
const webTargets = require('./web-targets.cjs');

function webSelection(selected = {}, sdkOnly = false) {
  const profile = webProfiles.resolveProfile(selected.webProfile);
  if (sdkOnly && profile !== 'distribution') throw new Error('Source diagnostics require an application Web build');
  return profile;
}
function defaultWebTarget(root) {
  const plan = webTargets.resolve(root);
  return plan.configured ? {...plan.targets.find(target => target.name === plan.default),
    configurationSha256: plan.configurationSha256} : null;
}
function verifyApplicationWeb(directory, profile, expectedTarget, autoProject) {
  if (autoProject) return require('./deploy-resources.cjs').verifyAutoDirectory(directory, profile, autoProject);
  const resources = path.join(directory, 'resources');
  const checked = webProfiles.verifyDirectory(resources, profile, {excludeLegal: true});
  if (expectedTarget) webTargets.verifyTargetDirectory(resources, expectedTarget,
    {excludeLegal: true, requireManifest: true});
  return checked;
}
function verifyApplicationRuntime(root, directory, architecture, renderer, projectRoot = root) {
  const runtime = require('./runtime-deployment.cjs');
  const project = path.join(projectRoot, 'project.json');
  const explicit = fs.existsSync(project) && Object.hasOwn(id.readJson(project), 'runtimeComponents');
  if (!fs.existsSync(path.join(directory, runtime.receiptName))) {
    if (explicit || renderer === 'auto') throw new Error('Explicit runtime components require a deployment receipt');
    return;
  }
  const receipt = runtime.verifyDirectory(directory);
  if (receipt.architecture !== architecture || receipt.backend !== renderer)
    throw new Error('Runtime architecture or renderer differs from this application build');
  if (fs.existsSync(path.join(root, 'tools/internal/runtime-files.json'))) {
    const expected = runtime.resolve(root, architecture, renderer, {projectRoot});
    if (JSON.stringify(receipt.components) !== JSON.stringify(expected.components) ||
        receipt.manifestSha256 !== expected.manifestSha256)
      throw new Error('Runtime component selection or manifest differs from this build');
    const actualFiles = receipt.files.map(file => file.path).sort();
    if (JSON.stringify(actualFiles) !== JSON.stringify(expected.files.map(file => file.path).sort()))
      throw new Error('Runtime deployment has an incomplete component file set');
    for (const file of expected.files) {
      if (file.path === 'node.dll' && receipt.kernel?.external) continue;
      const actual = receipt.files.find(actual => actual.path === file.path);
      if (actual.bytes !== file.bytes || actual.sha256 !== file.sha256)
        throw new Error('Runtime file differs from the SDK manifest');
    }
  }
}

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
function applicationFilesForTarget(root, target, application) {
  if (target !== 'SouiWorkbench' || fs.existsSync(id.safePath(root, 'project.json'))) return [];
  const helper = id.safePath(root, 'demos/workbench/tools/deploy-native-fixtures.cjs');
  if (!fs.existsSync(helper)) return [];
  const owner = require(helper);
  if (typeof owner.verify !== 'function') throw new Error('Workbench native fixture verifier is missing');
  return id.validateApplicationFiles(owner.verify(id.safePath(root, 'demos/workbench/fixtures'), application));
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
  if (profile !== undefined && !['compatibility', 'modern', 'auto'].includes(profile)) throw new Error('Invalid runtime profile');
  return profile === 'auto' ? 'auto' : profile === 'modern' ? 'webview2' : 'miniblink';
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
  if (/^SOUI_BUILD_AUTO_HOST:BOOL=ON\r?$/m.test(text)) {
    if (!libraries.webview2) throw new Error('Auto host requires the WebView2 library');
    libraries.autoHost = id.snapshot(dir, [prefix+'renderers/webview2/'+config+'/soui_auto_host.lib']);
  }
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
  const webProfile = webSelection(selected, sdkOnly);
  const dir = output(root, arch, config, sdkOnly);
  fs.mkdirSync(dir, {recursive: true});
  const reportFile = path.join(dir, `build-${config}.json`);
  // Invalidate before fingerprinting: a failed build must never leave an old success available.
  fs.writeFileSync(reportFile, id.encoded({schema: 'soui.source-build/v2', nativeBuild: 'pending', releaseReady: false}));
  const report = {schema: 'soui.source-build/v2', buildId: crypto.randomUUID(), version: version(root),
    architecture: arch, configuration: config, sdkOnly, webProfile, nativeBuild: 'pending', releaseReady: false,
    startedAt: new Date().toISOString(), nodeVersion: process.version, source: id.source(root)};
  if (selected.autoHost !== undefined && selected.autoHost !== 'enabled') throw new Error('Invalid auto host selection');
  if (selected.autoHost === 'enabled' || backend(root) === 'auto') report.autoHost = true;
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
  const webProfile = webSelection(selected, sdkOnly);
  if ((report.webProfile || 'distribution') !== webProfile) throw new Error('Web build profile changed during build');
  // Normal Build.ps1 records Web evidence before native work. Check it again
  // after deployment; legacy direct distribution callers still get the scan.
  if (report.webBuild || webProfile !== 'distribution') {
    const checked = webEvidence(root, arch, config, sdkOnly, false, selected);
    report.webBuild = {profile: checked.webProfile, artifactsSha256: checked.artifactsSha256};
  }
  if (!sdkOnly) for (const target of targets(root)) {
    verifyApplicationWeb(path.join(dir, 'bin', config, target), webProfile, defaultWebTarget(root), backend(root) === 'auto' ? root : null);
    verifyApplicationRuntime(root, path.join(dir, 'bin', config, target), arch, backend(root));
  }
  report.payloads = sdkOnly ? {} : Object.fromEntries(targets(root).map(target =>
    [target, id.payload(path.join(dir, 'bin', config, target), target, backend(root),
      applicationFilesForTarget(root, target, path.join(dir, 'bin', config, target)))]));
  if (sdkOnly) report.library = id.snapshot(dir, [`${config}/soui_sdk.lib`]);
  report.optionalLibraries = optionalLibraries(root, arch, config, sdkOnly);
  if (Boolean(report.autoHost) !== Boolean(report.optionalLibraries.autoHost)) throw new Error('Auto host build selection differs from configured library');
  if (!sdkOnly && !fs.existsSync(path.join(root, 'project.json')) && report.optionalLibraries.webview2) {
    verifyApplicationWeb(path.join(dir, 'bin', config, 'SouiModernStarter'), webProfile);
    verifyApplicationRuntime(root, path.join(dir, 'bin', config, 'SouiModernStarter'), arch, 'webview2');
    report.optionalPayloads = {SouiModernStarter: id.payload(path.join(dir, 'bin', config, 'SouiModernStarter'), 'SouiModernStarter', 'webview2')};
  }
  if (!sdkOnly && !fs.existsSync(path.join(root, 'project.json')) && report.optionalLibraries.autoHost) {
    const app = path.join(dir, 'bin', config, 'SouiAutoStarter'), project = path.join(root, 'templates/auto-app');
    verifyApplicationWeb(app, webProfile, null, project);
    verifyApplicationRuntime(root, app, arch, 'auto', project);
    report.optionalPayloads.SouiAutoStarter = id.payload(app, 'SouiAutoStarter', 'auto');
  }
  const requested=profileRequest(root,arch,config,sdkOnly,selected);
  if(JSON.stringify(requested)!==JSON.stringify(report.nativeToolchain||null))throw new Error('Requested toolchain changed during build');
  if(requested)report.nativeToolchain.observed={schema:'soui.llvm-config-files/v1',
    files:llvmConfigurationFiles(dir),compilerVerified:false,scope:'raw CMake configuration files only; actual compile/link observation pending'};
  report.nativeBuild = 'passed';
  report.finishedAt = new Date().toISOString();
  fs.writeFileSync(file, id.encoded(report));
  return {buildId: report.buildId, sourceSha256: report.source.sha256, targets: Object.keys(report.payloads)};
}
function webFiles(root, sdkOnly, selected) {
  const roots = ['sdk/web/dist', ...applicationWebRoots(root, sdkOnly, selected)];
  return roots.flatMap(name => id.walk(root, name));
}
function applicationWebRoots(root, sdkOnly, selected = {}) {
  if (sdkOnly) return [];
  if (fs.existsSync(path.join(root, 'project.json'))) return webTargets.resolve(root).targets.map(target => target.outDir);
  return ['templates/desktop-app/web/dist', 'templates/modern-app/web/dist', 'demos/workbench/web/dist',
    ...(selected.autoHost === 'enabled' ? webTargets.resolve(path.join(root, 'templates/auto-app')).targets.map(target => 'templates/auto-app/' + target.outDir) : [])];
}
function webEvidence(root, arch, config, sdkOnly, record, selected = {}) {
  const dir = output(root, arch, config, sdkOnly);
  const file = id.safePath(root, `out/web-${sdkOnly ? 'sdk' : 'applications'}.json`);
  const webProfile = webSelection(selected, sdkOnly);
  const applications = applicationWebRoots(root, sdkOnly, selected).map(relative => {
    const directory = id.safePath(root, relative);
    const checked = webProfiles.verifyDirectory(directory, webProfile);
    const projectRoot = relative.startsWith('templates/auto-app/') ? path.join(root, 'templates/auto-app') : root;
    const plan = webTargets.resolve(projectRoot);
    if (plan.configured) {
      const target = plan.targets.find(target => path.resolve(projectRoot, target.outDir) === path.resolve(root, relative));
      webTargets.verifyTargetDirectory(directory, {...target, configurationSha256: plan.configurationSha256},
        {requireManifest: true});
    }
    const manifest = path.join(directory, webProfiles.manifestName);
    return {root: relative, profile: checked.profile, marked: checked.marked,
      manifestSha256: checked.marked ? id.hash(fs.readFileSync(manifest)) : null};
  });
  const current = {schema: 'soui.web-build/v2', webProfile, applications, sourceSha256: id.source(root).sha256,
    artifacts: id.snapshot(root, webFiles(root, sdkOnly, selected))};
  if (record) {
    const started = id.readJson(path.join(dir, `build-${config}.json`));
    if (started.nativeBuild !== 'pending' || started.source.sha256 !== current.sourceSha256) throw new Error('Source changed during Web build');
    if ((started.webProfile || 'distribution') !== webProfile) throw new Error('Web profile changed during Web build');
    fs.writeFileSync(file, id.encoded(current));
    started.webBuild = {profile: webProfile, artifactsSha256: current.artifacts.sha256};
    fs.writeFileSync(path.join(dir, `build-${config}.json`), id.encoded(started));
  } else {
    if (!fs.existsSync(file) || JSON.stringify(id.readJson(file)) !== JSON.stringify(current)) throw new Error('SkipWeb requires matching Web source and output evidence; build without -SkipWeb');
    const buildFile = path.join(dir, `build-${config}.json`);
    if (fs.existsSync(buildFile)) {
      const started = id.readJson(buildFile);
      // SkipWeb is also a Web step in the current build. Bind its successful
      // observation so finish rechecks output after native compilation.
      // Standalone readers of an already finished build remain read-only.
      if (started.nativeBuild === 'pending') {
        if (started.source?.sha256 !== current.sourceSha256) throw new Error('Source changed during Web build');
        if ((started.webProfile || 'distribution') !== webProfile) throw new Error('Web profile changed during Web build');
        if (started.webBuild && (started.webBuild.profile !== webProfile ||
            started.webBuild.artifactsSha256 !== current.artifacts.sha256)) throw new Error('Web output changed after this build accepted it');
        started.webBuild = {profile: webProfile, artifactsSha256: current.artifacts.sha256};
        fs.writeFileSync(buildFile, id.encoded(started));
      }
    }
  }
  return {webVerified: true, webProfile, artifactsSha256: current.artifacts.sha256};
}
function deployWeb(root, arch, config, sdkOnly = false, selected = {}) {
  if (sdkOnly) throw new Error('SDK-only builds have no application Web deployment');
  const profile = webSelection(selected);
  const dir = output(root, arch, config);
  const started = id.readJson(path.join(dir, `build-${config}.json`));
  if (started.nativeBuild !== 'pending' || started.source.sha256 !== id.source(root).sha256 ||
      (started.webProfile || 'distribution') !== profile) throw new Error('Web deployment requires the matching pending build');
  webEvidence(root, arch, config, false, false, selected);
  const generated = fs.existsSync(path.join(root, 'project.json'));
  const entries = generated ? [[targets(root)[0], defaultWebTarget(root)?.outDir || 'web/dist']] :
    [['SouiStarter', 'templates/desktop-app/web/dist'], ['SouiWorkbench', 'demos/workbench/web/dist']];
  if (!generated && optionalLibraries(root, arch, config, false).webview2)
    entries.push(['SouiModernStarter', 'templates/modern-app/web/dist']);
  if (!generated && optionalLibraries(root, arch, config, false).autoHost)
    entries.push(['SouiAutoStarter', 'templates/auto-app']);
  const deploy = require('./deploy-resources.cjs');
  for (const [target, relative] of entries) {
    const application = id.safePath(dir, `bin/${config}/${target}`);
    if (!fs.statSync(id.safePath(application, `${target}.exe`)).isFile()) throw new Error('Native application is absent before Web deployment');
    if ((generated && backend(root) === 'auto') || target === 'SouiAutoStarter')
      deploy.replaceAuto(generated ? root : path.join(root, 'templates/auto-app'), application, root, profile);
    else deploy.replace(id.safePath(root, relative), application, root, profile, defaultWebTarget(root));
  }
  return {webDeployed: true, webProfile: profile, targets: entries.map(([target]) => target)};
}
function verifyBuild(root, arch, config, sdkOnly = false, selected = {}) {
  const dir = output(root, arch, config, sdkOnly);
  const report = id.readJson(path.join(dir, `build-${config}.json`));
  if (report.schema !== 'soui.source-build/v2' || report.nativeBuild !== 'passed' || !report.buildId ||
      report.version !== version(root) || report.architecture !== arch || report.configuration !== config || report.sdkOnly !== sdkOnly ||
      !report.source || report.source.sha256 !== id.source(root).sha256) throw new Error('Build evidence is stale or incomplete; rebuild current source');
  const recordedWebProfile = webProfiles.resolveProfile(report.webProfile);
  const expectedWebProfile = webSelection({webProfile: selected.webProfile ?? recordedWebProfile}, sdkOnly);
  if (recordedWebProfile !== expectedWebProfile) throw new Error('Build evidence Web profile differs; no cross-profile result reuse');
  const actualProfile=report.nativeToolchain?.profile||'MSVC143',expectedProfile=selected.nativeToolchain||'MSVC143';
  if(actualProfile!==expectedProfile)throw new Error('Build evidence toolchain profile differs; no cross-profile result reuse');
  if(actualProfile==='LLVM22MSVC142') {
    const requested=profileRequest(root,arch,config,sdkOnly,selected);
    if(JSON.stringify(requested.requestFile)!==JSON.stringify(report.nativeToolchain.requestFile)||
        JSON.stringify(llvmConfigurationFiles(dir))!==JSON.stringify(report.nativeToolchain.observed?.files))throw new Error('LLVM request/configuration files drift');
  }
  if (!sdkOnly) {
    if (webTargets.resolve(root).configured || report.autoHost) {
      const checked = webEvidence(root, arch, config, false, false, {webProfile: recordedWebProfile, ...(report.autoHost ? {autoHost: 'enabled'} : {})});
      if (report.webBuild?.artifactsSha256 !== checked.artifactsSha256)
        throw new Error('Web target outputs differ from the accepted build');
    }
    if (JSON.stringify(Object.keys(report.payloads || {}).sort()) !== JSON.stringify(targets(root).sort())) throw new Error('Build payload targets missing');
    for (const target of targets(root)) {
      verifyApplicationWeb(path.join(dir, 'bin', config, target), recordedWebProfile, defaultWebTarget(root), backend(root) === 'auto' ? root : null);
      verifyApplicationRuntime(root, path.join(dir, 'bin', config, target), arch, backend(root));
      const applicationFiles = applicationFilesForTarget(root, target, path.join(dir, 'bin', config, target));
      if (JSON.stringify(report.payloads[target].applicationFiles || []) !== JSON.stringify(applicationFiles))
        throw new Error('Application data ownership differs from accepted build');
      id.verifyPayload(path.join(dir, 'bin', config, target), target, report.payloads[target], backend(root));
    }
  } else if (!report.library || id.snapshot(dir, [`${config}/soui_sdk.lib`]).sha256 !== report.library.sha256) throw new Error('SDK library drift');
  if (JSON.stringify(report.optionalLibraries || {}) !== JSON.stringify(optionalLibraries(root, arch, config, sdkOnly)))
    throw new Error('Optional SDK library or component configuration drift');
  if (Boolean(report.autoHost) !== Boolean(report.optionalLibraries?.autoHost)) throw new Error('Auto host library evidence missing');
  if (!sdkOnly && !fs.existsSync(path.join(root, 'project.json')) && report.optionalLibraries?.webview2) {
    verifyApplicationWeb(path.join(dir, 'bin', config, 'SouiModernStarter'), recordedWebProfile);
    verifyApplicationRuntime(root, path.join(dir, 'bin', config, 'SouiModernStarter'), arch, 'webview2');
    id.verifyPayload(path.join(dir, 'bin', config, 'SouiModernStarter'), 'SouiModernStarter', report.optionalPayloads?.SouiModernStarter, 'webview2');
  }
  if (!sdkOnly && !fs.existsSync(path.join(root, 'project.json')) && report.optionalLibraries?.autoHost) {
    const app = path.join(dir, 'bin', config, 'SouiAutoStarter'), project = path.join(root, 'templates/auto-app');
    verifyApplicationWeb(app, recordedWebProfile, null, project);
    verifyApplicationRuntime(root, app, arch, 'auto', project);
    id.verifyPayload(app, 'SouiAutoStarter', report.optionalPayloads?.SouiAutoStarter, 'auto');
  }
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
  const effective = {...inferred, webProfile: webProfiles.resolveProfile(current.webProfile), ...selected};
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
function verifyAutoSmoke(root, arch, config, selected = {}, toolsRoot = root) {
  const verified = verifyAcceptedBuild(root, arch, config, false, selected), {build} = verified;
  const generated = fs.existsSync(path.join(root, 'project.json'));
  if (generated && backend(root) !== 'auto') throw new Error('Not an auto application');
  const target = generated ? targets(root)[0] : 'SouiAutoStarter';
  const payload = generated ? build.payloads[target] : build.optionalPayloads?.[target];
  if (!build.optionalLibraries?.autoHost || !payload) throw new Error('Auto application build missing');
  const smoke = id.readJson(path.join(output(root, arch, config), `auto-smoke-${config}.json`));
  require('./auto-smoke.cjs').validateReport(root, smoke, build, payload, verified.selected, toolsRoot);
  return {build, smoke};
}
function stageApp(root, arch, config, target, destination, selected = {}) {
  const generated = fs.existsSync(id.safePath(root, 'project.json'));
  const optionalModern = !generated && target === 'SouiModernStarter';
  const optionalAuto = !generated && target === 'SouiAutoStarter';
  if (!targets(root).includes(target) && !optionalModern && !optionalAuto) throw new Error('Unknown application target');
  const automatic = optionalAuto || backend(root) === 'auto';
  const modern = optionalModern || backend(root) === 'webview2';
  const {build} = automatic ? verifyAutoSmoke(root, arch, config, selected) : modern ? verifyModernSmoke(root, arch, config, selected) : verifySmoke(root, arch, config, selected);
  const expected = optionalModern || optionalAuto ? build.optionalPayloads[target] : build.payloads[target];
  destination = id.safePath(destination);
  if (fs.existsSync(destination)) throw new Error('Application stage already exists');
  const app = path.join(output(root, arch, config), 'bin', config, target);
  for (const entry of expected.files) {
    const to = id.safePath(destination, entry.path);
    fs.mkdirSync(path.dirname(to), {recursive: true});
    fs.copyFileSync(id.safePath(app, entry.path), to);
  }
  const payload = id.verifyPayload(destination, target, expected, automatic ? 'auto' : modern ? 'webview2' : 'miniblink');
  const categories = {executable: 0, kernel: 0, libraries: 0, web: 0};
  for (const file of payload.files) categories[file.path.startsWith('resources/') ? 'web' : file.path === 'node.dll' ? 'kernel' : file.path === `${target}.exe` ? 'executable' : 'libraries'] += file.bytes;
  const report = {schema: 'soui.application-size/v1', version: build.version, target, architecture: arch, configuration: config,
    rendererBackend: automatic ? 'auto' : modern ? 'webview2' : 'miniblink', webTarget: automatic ? null : defaultWebTarget(root)?.name || null,
    buildId: build.buildId, sourceSha256: build.source.sha256, payload, categories, releaseReady: false};
  report.sizeAnalysis = require('./size-report.cjs').summarize(report);
  return report;
}
module.exports = {version, targets, start, finish, webEvidence, deployWeb, verifyBuild, verifyAcceptedBuild, beginSmoke, validateRuns, finishSmoke, verifySmoke, validateModernSmoke, verifyModernSmoke, verifyAutoSmoke, stageApp, consoleJson};
if (require.main === module) {
  try {
    const [command, arch, config, ...args] = process.argv.slice(2);
    const root = path.resolve(__dirname, '../..');
    const commands = {start, finish, 'deploy-web': deployWeb, 'verify-build': verifyBuild, 'begin-smoke': beginSmoke, 'finish-smoke': finishSmoke,
      'verify-smoke': verifySmoke, 'verify-modern-smoke': verifyModernSmoke, 'verify-auto-smoke': verifyAutoSmoke,
      'record-web': (...a) => webEvidence(...a, true), 'verify-web': (...a) => webEvidence(...a, false)};
    let result;
    if (commands[command] || command === 'stage-app') {
      const remaining=[...args],sdkOnly=remaining[0]==='sdk-only';if(sdkOnly)remaining.shift();
      const stageArguments = command === 'stage-app' ? remaining.splice(0, 2) : [];
      if(command==='stage-app' && (sdkOnly || stageArguments.length!==2))throw new Error('Stage requires target and destination');
      const selected={},seen=new Set();
      while(remaining.length){const flag=remaining.shift(),key={'--native-toolchain':'nativeToolchain','--profile-request':'profileRequest','--web-profile':'webProfile','--auto-host':'autoHost'}[flag];
        if(!key||seen.has(key)||remaining.length===0)throw new Error('Invalid build-evidence profile arguments');seen.add(key);selected[key]=remaining.shift();}
      if((selected.nativeToolchain||selected.profileRequest)&&(!selected.nativeToolchain||!selected.profileRequest))throw new Error('Both LLVM evidence arguments are required');
      if(command==='stage-app')result=stageApp(root,arch,config,...stageArguments,selected);
      else if(['record-web','verify-web'].includes(command)){if(selected.nativeToolchain||selected.profileRequest)throw new Error('Web evidence does not select a native profile');result=webEvidence(root,arch,config,sdkOnly,command==='record-web',selected);}
      else if(command==='verify-build')result=verifyAcceptedBuild(root,arch,config,sdkOnly,selected).build;
      else if(['start','finish','deploy-web'].includes(command))result=commands[command](root,arch,config,sdkOnly,selected);
      else {if(sdkOnly)throw new Error('Smoke requires application build');result=commands[command](root,arch,config,selected);}
    }
    else throw new Error('Invalid build-evidence command/arguments');
    console.log(consoleJson(result));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
