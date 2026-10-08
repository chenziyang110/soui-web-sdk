'use strict';
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const id = require('./delivery-identity.cjs');

function version(root) {
  const lockFile = id.safePath(root, 'sdk-lock.json');
  const value = fs.existsSync(lockFile) ? id.readJson(lockFile).manifest.version : fs.readFileSync(id.safePath(root, 'VERSION'), 'utf8').trim();
  if (!/^\d+\.\d+\.\d+-preview\.\d+$/.test(value)) throw new Error('Invalid preview VERSION');
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
  const dir = output(root, arch, config, sdkOnly);
  const cache = path.join(dir, 'CMakeCache.txt');
  if (!fs.existsSync(cache) || !/^SOUI_BUILD_WEBVIEW2:BOOL=ON\r?$/m.test(fs.readFileSync(cache, 'utf8'))) return {};
  return {webview2: id.snapshot(dir, [`${sdkOnly ? '' : 'sdk/'}renderers/webview2/${config}/soui_webview2.lib`])};
}
function start(root, arch, config, sdkOnly = false) {
  const dir = output(root, arch, config, sdkOnly);
  fs.mkdirSync(dir, {recursive: true});
  const reportFile = path.join(dir, `build-${config}.json`);
  // Invalidate before fingerprinting: a failed build must never leave an old success available.
  fs.writeFileSync(reportFile, id.encoded({schema: 'soui.source-build/v2', nativeBuild: 'pending', releaseReady: false}));
  const report = {schema: 'soui.source-build/v2', buildId: crypto.randomUUID(), version: version(root),
    architecture: arch, configuration: config, sdkOnly, nativeBuild: 'pending', releaseReady: false,
    startedAt: new Date().toISOString(), nodeVersion: process.version, source: id.source(root)};
  fs.writeFileSync(reportFile, id.encoded(report));
  return {buildId: report.buildId, sourceSha256: report.source.sha256};
}
function finish(root, arch, config, sdkOnly = false) {
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
function verifyBuild(root, arch, config, sdkOnly = false) {
  const dir = output(root, arch, config, sdkOnly);
  const report = id.readJson(path.join(dir, `build-${config}.json`));
  if (report.schema !== 'soui.source-build/v2' || report.nativeBuild !== 'passed' || !report.buildId ||
      report.version !== version(root) || report.architecture !== arch || report.configuration !== config || report.sdkOnly !== sdkOnly ||
      !report.source || report.source.sha256 !== id.source(root).sha256) throw new Error('Build evidence is stale or incomplete; rebuild current source');
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
function beginSmoke(root, arch, config) {
  const dir = output(root, arch, config);
  fs.writeFileSync(path.join(dir, `smoke-${config}.json`), id.encoded({schema: 'soui.native-smoke/v2', passed: false, releaseReady: false}));
  const build = verifyBuild(root, arch, config);
  return {schema: 'soui.native-smoke/v2', buildId: build.buildId, sourceSha256: build.source.sha256,
    version: build.version, architecture: arch, configuration: config, releaseReady: false,
    startedAt: new Date().toISOString(), scenarios: scenarios(root)};
}
function validateRuns(root, report, build) {
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
}
function finishSmoke(root, arch, config) {
  const dir = output(root, arch, config);
  const file = path.join(dir, `smoke-${config}.json`);
  const report = id.readJson(file);
  const build = verifyBuild(root, arch, config);
  validateRuns(root, report, build);
  report.payloads = build.payloads;
  report.passed = true;
  report.finishedAt = new Date().toISOString();
  fs.writeFileSync(file, id.encoded(report));
  return {passed: true, buildId: build.buildId, scenarios: report.runs.length, releaseReady: false};
}
function verifySmoke(root, arch, config) {
  const dir = output(root, arch, config);
  const build = verifyBuild(root, arch, config);
  const smoke = id.readJson(path.join(dir, `smoke-${config}.json`));
  validateRuns(root, smoke, build);
  if (smoke.passed !== true || !smoke.finishedAt || JSON.stringify(smoke.payloads) !== JSON.stringify(build.payloads)) throw new Error('Smoke payload identity missing');
  return {build, smoke};
}
function validateModernSmoke(build, smoke, target, payload) {
  if (!build.optionalLibraries?.webview2 || !payload || smoke.schema !== 'soui.modern-smoke/1' ||
      smoke.version !== build.version || smoke.buildId !== build.buildId || smoke.sourceSha256 !== build.source.sha256 ||
      smoke.architecture !== build.architecture || smoke.configuration !== build.configuration || smoke.target !== target ||
      smoke.passed !== true || smoke.exitCode !== 0 || smoke.timedOut !== false || !smoke.finishedAt ||
      !Number.isSafeInteger(smoke.pid) || smoke.pid <= 0 || JSON.stringify(smoke.payload) !== JSON.stringify(payload) ||
      typeof smoke.log !== 'string' || !['application-ready', 'host-close-complete', 'host-started;runtime='].every(text => smoke.log.includes(text)))
    throw new Error('Modern smoke identity or lifecycle evidence is incomplete');
}
function verifyModernSmoke(root, arch, config) {
  const build = verifyBuild(root, arch, config);
  const generated = fs.existsSync(path.join(root, 'project.json'));
  if (generated && backend(root) !== 'webview2') throw new Error('Not a modern application');
  const target = generated ? targets(root)[0] : 'SouiModernStarter';
  const payload = generated ? build.payloads[target] : build.optionalPayloads?.[target];
  const smoke = id.readJson(path.join(output(root, arch, config), `modern-smoke-${config}.json`));
  validateModernSmoke(build, smoke, target, payload);
  return {build, smoke};
}
function stageApp(root, arch, config, target, destination) {
  if (!targets(root).includes(target)) throw new Error('Unknown application target');
  const {build} = verifySmoke(root, arch, config);
  destination = id.safePath(destination);
  if (fs.existsSync(destination)) throw new Error('Application stage already exists');
  const app = path.join(output(root, arch, config), 'bin', config, target);
  for (const entry of build.payloads[target].files) {
    const to = id.safePath(destination, entry.path);
    fs.mkdirSync(path.dirname(to), {recursive: true});
    fs.copyFileSync(id.safePath(app, entry.path), to);
  }
  const payload = id.verifyPayload(destination, target, build.payloads[target], backend(root));
  const categories = {executable: 0, kernel: 0, libraries: 0, web: 0};
  for (const file of payload.files) categories[file.path.startsWith('resources/') ? 'web' : file.path === 'node.dll' ? 'kernel' : file.path === `${target}.exe` ? 'executable' : 'libraries'] += file.bytes;
  return {schema: 'soui.application-size/v1', version: build.version, target, architecture: arch, configuration: config,
    buildId: build.buildId, sourceSha256: build.source.sha256, payload, categories, releaseReady: false};
}
module.exports = {version, targets, start, finish, verifyBuild, beginSmoke, validateRuns, finishSmoke, verifySmoke, validateModernSmoke, verifyModernSmoke, stageApp};
if (require.main === module) {
  try {
    const [command, arch, config, ...args] = process.argv.slice(2);
    const root = path.resolve(__dirname, '../..');
    const commands = {start, finish, 'verify-build': verifyBuild, 'begin-smoke': beginSmoke, 'finish-smoke': finishSmoke,
      'verify-smoke': verifySmoke, 'record-web': (...a) => webEvidence(...a, true), 'verify-web': (...a) => webEvidence(...a, false)};
    let result;
    if (command === 'stage-app' && args.length === 2) result = stageApp(root, arch, config, ...args);
    else if (commands[command] && args.length <= 1 && (!args.length || args[0] === 'sdk-only')) result = commands[command](root, arch, config, args[0] === 'sdk-only');
    else throw new Error('Invalid build-evidence command/arguments');
    console.log(id.encoded(result));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
