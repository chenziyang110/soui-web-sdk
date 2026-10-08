'use strict';
const fs = require('node:fs');
const path = require('node:path');
const os = require('node:os');
const crypto = require('node:crypto');
const {execFileSync, spawnSync} = require('node:child_process');
const identity = require('../../tools/internal/delivery-identity.cjs');
const builds = require('../../tools/internal/build-evidence.cjs');

const root = path.resolve(__dirname, '../..');
const destination = process.argv[2] && path.resolve(process.argv[2]);
if (process.argv.length !== 3 || !destination || !path.isAbsolute(process.argv[2]) ||
    destination.startsWith(root + path.sep) || fs.existsSync(destination))
  throw new Error('Usage: node benchmarks/size/measure.cjs ABSOLUTE_NEW_OUTPUT_DIRECTORY outside the source tree');

function sha256(bytes) { return crypto.createHash('sha256').update(bytes).digest('hex'); }
function filesUnder(directory) {
  const files = [];
  function visit(folder, prefix = '') {
    for (const entry of fs.readdirSync(folder, {withFileTypes: true})) {
      const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
      const source = path.join(folder, entry.name);
      if (entry.isDirectory()) visit(source, relative);
      else if (entry.isFile()) {
        const bytes = fs.readFileSync(source);
        files.push({path: relative, bytes: bytes.length, sha256: sha256(bytes)});
      } else throw new Error(`Unsupported staged entry: ${relative}`);
    }
  }
  visit(directory);
  files.sort((a, b) => a.path.localeCompare(b.path, 'en'));
  return {files, bytes: files.reduce((sum, file) => sum + file.bytes, 0),
    sha256: sha256(Buffer.from(JSON.stringify(files)))};
}
function copy(source, target) {
  if (!fs.statSync(source).isFile() || fs.existsSync(target)) throw new Error(`Missing source or duplicate target: ${target}`);
  fs.mkdirSync(path.dirname(target), {recursive: true});
  fs.copyFileSync(source, target);
}
function archive(directory, target) {
  execFileSync('7z.exe', ['a', '-tzip', '-mx=5', target, '.\\*'], {cwd: directory, windowsHide: true,
    stdio: 'ignore', timeout: 300000});
  execFileSync('7z.exe', ['t', target], {windowsHide: true, stdio: 'ignore', timeout: 300000});
  const bytes = fs.readFileSync(target);
  return {bytes: bytes.length, sha256: sha256(bytes)};
}
function probe(executable, args, cwd, output) {
  fs.mkdirSync(output, {recursive: true});
  const run = spawnSync(executable, args, {cwd, windowsHide: true, timeout: 45000, maxBuffer: 2 * 1024 * 1024});
  const stdout = run.stdout || Buffer.alloc(0);
  const stderr = run.stderr || Buffer.alloc(0);
  fs.writeFileSync(path.join(output, 'stdout.log'), stdout);
  fs.writeFileSync(path.join(output, 'stderr.log'), stderr);
  if (run.error || run.signal || run.status !== 0)
    throw new Error(`Runtime probe failed: ${executable}; exit=${run.status}; signal=${run.signal}; error=${run.error?.message || 'none'}`);
  return {exitCode: run.status, stdoutSha256: sha256(stdout), stderrSha256: sha256(stderr)};
}
function singleResult(directory, hostKind) {
  const files = fs.readdirSync(directory).filter(name => name.endsWith('.json'));
  if (files.length !== 1) throw new Error(`Expected one ${hostKind} probe result, found ${files.length}`);
  const bytes = fs.readFileSync(path.join(directory, files[0]));
  const result = JSON.parse(bytes.toString('utf8'));
  if (result.status !== 'measured' || result.scene !== 'S01' || result.hostKind !== hostKind ||
      result.fixture?.fixtureId !== 'electron-smoothness-10k-v1' || result.fixture?.rowCount !== 10000 ||
      result.js?.eventLog?.length !== 0 || result.js?.rafIntervalsMs?.length !== 0)
    throw new Error(`Invalid ${hostKind} probe result`);
  return {resultSha256: sha256(bytes), scene: result.scene, fixtureId: result.fixture.fixtureId};
}

const sdkBuild = builds.verifyBuild(root, 'x64', 'Release', true);
const installedLibrary = path.join(root, 'out/install/x64/lib/Release/soui_sdk.lib');
if (sha256(fs.readFileSync(installedLibrary)) !== sdkBuild.library.files[0].sha256)
  throw new Error('Installed public SDK library differs from its verified build');
const souiExe = path.join(root, 'out/benchmark-soui-x64-19041/bin/Release/SouiBenchmark.exe');
const electronRoot = path.join(root, 'benchmarks/hosts/electron');
const electronDist = path.join(electronRoot, 'node_modules/electron/dist');
const electronVersion = JSON.parse(fs.readFileSync(path.join(electronRoot, 'node_modules/electron/package.json'), 'utf8')).version;
const pinnedVersion = JSON.parse(fs.readFileSync(path.join(electronRoot, 'package.json'), 'utf8')).devDependencies.electron;
if (electronVersion !== pinnedVersion || fs.readFileSync(path.join(electronDist, 'version'), 'utf8').trim() !== pinnedVersion)
  throw new Error('Electron package, pin, and distribution versions differ');
const page = path.join(root, 'demos/workbench/web/benchmarks/electron-smoothness/dist-legacy');
const benchmarkHtml = fs.readFileSync(path.join(page, 'benchmark.html'));
if (!benchmarkHtml.equals(fs.readFileSync(path.join(page, 'index.html'))))
  throw new Error('The SOUI and Electron benchmark entry pages differ');
const inputPage = filesUnder(page);
const sourceSha256 = sdkBuild.source.sha256;

fs.mkdirSync(destination);
const soui = path.join(destination, 'soui');
const electron = path.join(destination, 'electron');
fs.mkdirSync(soui);
fs.cpSync(electronDist, electron, {recursive: true, errorOnExist: true, force: false});
copy(souiExe, path.join(soui, 'SouiBenchmark.exe'));
copy(path.join(root, 'third_party/prebuilt/x64/miniblink/node.dll'), path.join(soui, 'node.dll'));
for (const subdir of ['runtime', 'soui/bin']) {
  const input = path.join(root, 'third_party/prebuilt/x64', subdir);
  for (const name of fs.readdirSync(input)) {
    if (!/\.dll$/i.test(name) || /(?:souid|utilitiesd|render-gdid|imgdecoder-gdipd|soui-sys-resourced)\.dll$/i.test(name)) continue;
    copy(path.join(input, name), path.join(soui, name));
  }
}
for (const name of ['LICENSE.md', 'THIRD_PARTY_NOTICES.md']) copy(path.join(root, name), path.join(soui, name));
for (const name of ['dependencies.json', 'license-review.json'])
  copy(path.join(root, 'third_party/manifests', name), path.join(soui, 'licenses', name));
fs.cpSync(page, path.join(soui, 'resources'), {recursive: true, errorOnExist: true, force: false});

const electronApp = path.join(electron, 'resources/app');
fs.mkdirSync(electronApp);
for (const name of ['main.cjs', 'preload.cjs', 'package.json']) copy(path.join(electronRoot, name), path.join(electronApp, name));
fs.cpSync(page, path.join(electronApp, 'page'), {recursive: true, errorOnExist: true, force: false});
const stagedSouiPage = filesUnder(path.join(soui, 'resources'));
const stagedElectronPage = filesUnder(path.join(electronApp, 'page'));
if (inputPage.sha256 !== stagedSouiPage.sha256 || inputPage.sha256 !== stagedElectronPage.sha256)
  throw new Error('The staged hosts do not contain the same shared page');
if (identity.source(root).sha256 !== sourceSha256) throw new Error('Source changed during packaging');

const souiFiles = filesUnder(soui);
const electronFiles = filesUnder(electron);
const souiZip = archive(soui, path.join(destination, 'soui.zip'));
const electronZip = archive(electron, path.join(destination, 'electron.zip'));
const smoke = path.join(destination, 'smoke');
const souiProbeStage = path.join(smoke, 'soui-stage');
fs.mkdirSync(smoke);
fs.cpSync(soui, souiProbeStage, {recursive: true, errorOnExist: true, force: false});
fs.copyFileSync(path.join(souiProbeStage, 'resources/benchmark-smoke.html'),
  path.join(souiProbeStage, 'resources/index.html'));
const souiProbeResults = path.join(smoke, 'soui-results');
const souiProbe = probe(path.join(souiProbeStage, 'SouiBenchmark.exe'), [
  `--resources=${path.join(souiProbeStage, 'resources')}`,
  `--miniblink=${path.join(souiProbeStage, 'node.dll')}`,
  `--result-dir=${souiProbeResults}`, '--smoke'], souiProbeStage, path.join(smoke, 'soui-logs'));
Object.assign(souiProbe, singleResult(souiProbeResults, 'soui-miniblink'));
const electronProbeResults = path.join(smoke, 'electron-results');
const electronProbe = probe(path.join(electron, 'electron.exe'), [
  `--benchmark-page=${path.join(electronApp, 'page/benchmark.html')}`,
  `--result-dir=${electronProbeResults}`, '--smoke'], electron, path.join(smoke, 'electron-logs'));
const electronResults = fs.readdirSync(electronProbeResults).filter(name => name.endsWith('.json'));
const metadataFile = electronResults.find(name => name.endsWith('-host.json'));
if (electronResults.length !== 2 || !metadataFile) throw new Error('Expected one Electron host record and one result');
const metadataBytes = fs.readFileSync(path.join(electronProbeResults, metadataFile));
const metadata = JSON.parse(metadataBytes.toString('utf8'));
if (metadata.host !== 'electron' || metadata.versions?.electron !== electronVersion ||
    metadata.pageSha256 !== sha256(benchmarkHtml) || metadata.security?.sandbox !== true ||
    metadata.security?.contextIsolation !== true || metadata.security?.nodeIntegration !== false ||
    metadata.security?.webSecurity !== true) throw new Error('Invalid Electron host probe metadata');
const electronResultFile = electronResults.find(name => name !== metadataFile);
const electronResultDir = path.join(smoke, 'electron-result-only');
fs.mkdirSync(electronResultDir);
fs.copyFileSync(path.join(electronProbeResults, electronResultFile), path.join(electronResultDir, electronResultFile));
Object.assign(electronProbe, singleResult(electronResultDir, 'electron'),
  {metadataSha256: sha256(metadataBytes)});
if (identity.source(root).sha256 !== sourceSha256) throw new Error('Source changed during runtime probing');
const report = {schema: 'soui.benchmark-package-size/v1', measuredAt: new Date().toISOString(),
  releaseReady: false, benchmarkValid: false, platform: os.platform(), osRelease: os.release(), architecture: os.arch(),
  sourceSha256, sdkBuildId: sdkBuild.buildId, sdkVersion: sdkBuild.version,
  electronVersion, sharedPage: inputPage, benchmarkHtmlSha256: sha256(benchmarkHtml),
  soui: {...souiFiles, zip: souiZip}, electron: {...electronFiles, zip: electronZip},
  ratios: {uncompressed: souiFiles.bytes / electronFiles.bytes, compressed: souiZip.bytes / electronZip.bytes},
  smoke: {probeOnly: true, soui: souiProbe, electron: electronProbe},
  scope: 'Minimal x64 Release hosts with the same local page and app-local runtime files; excludes installer, signing, and unverified third-party redistribution obligations.'};
fs.writeFileSync(path.join(destination, 'report.json'), JSON.stringify(report, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({report: path.join(destination, 'report.json'), sourceSha256, souiBytes: souiFiles.bytes,
  electronBytes: electronFiles.bytes, souiZipBytes: souiZip.bytes, electronZipBytes: electronZip.bytes,
  ratios: report.ratios}, null, 2));
