'use strict';
// Prepared only. Run by the VM operator after the final, byte-verified copy.
// This controller uses current public entries; it never fabricates their receipts.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const root = path.resolve(process.argv[2] || 'C:/sr06');
const output = path.resolve(process.argv[3] || 'C:/release06-control/evidence/owned-' + crypto.randomUUID());
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function need(ok, message) { if (!ok) throw Error(message); }
function pin(file) { const bytes = fs.readFileSync(file); return {path: file, bytes: bytes.length, sha256: hash(bytes)}; }
function write(name, value) { fs.writeFileSync(path.join(output, name), JSON.stringify(value, null, 2) + '\n', {flag: 'wx'}); }
const allStages = ['x86:desktop', 'x86:modern', 'x64:desktop', 'x64:modern'];
const selection = process.argv[4] === undefined ? [...allStages] : process.argv[4].split(',');
need(selection.length > 0 && selection.every(stage => allStages.includes(stage)) &&
  new Set(selection).size === selection.length, 'Select unique nonempty stages from: ' + allStages.join(','));
need(process.platform === 'win32' && root.toLowerCase() === path.resolve('C:/sr06').toLowerCase(), 'Use the unchanged ordinary guest C:/sr06 root');
need(process.arch === 'x64', 'Use the pinned x64 controller Node and x64 outer Runner');
need(!output.toLowerCase().startsWith(root.toLowerCase() + path.sep), 'Controller evidence must be outside the source root');
need(output.toLowerCase().startsWith(path.resolve('C:/release06-control/evidence').toLowerCase() + path.sep), 'Use the designated fresh controller evidence root');
need(!fs.existsSync(output), 'Use a fresh evidence directory; preserve prior failures');
fs.mkdirSync(path.dirname(output), {recursive: true}); fs.mkdirSync(output);
const standard = require(path.join(root, 'tools/internal/standard-build.cjs'));
const {validateStage} = require(path.join(root, 'tools/internal/devkit-command.cjs'));
const measurement = require(path.join(root, 'tools/internal/measurement-validation.cjs'));
const evidence = require(path.join(root, 'tools/internal/build-evidence.cjs'));
const id = require(path.join(root, 'tools/internal/delivery-identity.cjs'));
const powershell = path.join(process.env.SystemRoot, 'System32/WindowsPowerShell/v1.0/powershell.exe');
const outerBudgets = Object.freeze({desktop: Object.freeze({timeoutMs: 180000, drainMs: 2000}),
  modern: Object.freeze({timeoutMs: 90000, drainMs: 2000})});
process.env.PATH = path.dirname(process.execPath) + path.delimiter + (process.env.PATH || '');
const before = [], stages = [], attemptsBefore = new Set(), snapshotArchitectures = new Set(), attempted = new Set(), files = new Set();
let failed = null;
function collect(file) { if (fs.existsSync(file)) { id.safePath(file); if (fs.statSync(file).isFile()) files.add(file); } }
function collectTree(folder) { if (fs.existsSync(folder)) for (const name of id.walk(folder)) collect(path.join(folder, name)); }
function attemptDirectories(architecture) {
  const folder = path.join(root, 'out', architecture);
  return fs.readdirSync(folder, {withFileTypes: true}).filter(item => item.isDirectory() &&
    /^(smoke-owned-|modern-owned-)/.test(item.name)).map(item => path.join(folder, item.name));
}
async function main() {
  try {
    // MSVC readers are filesystem checks. Do not erase an actual LLVM selection.
    for (const architecture of ['x86', 'x64']) {
      const buildFile = path.join(root, 'out', architecture, 'build-Release.json');
      const accepted = evidence.verifyAcceptedBuild(root, architecture, 'Release', false);
      need((accepted.build.nativeToolchain?.profile || 'MSVC143') === 'MSVC143', 'This plan is for the actual MSVC143 app builds only');
      need(Boolean(accepted.build.optionalLibraries?.webview2), 'Current Modern payload is required');
      const setup = measurement.preparedAt(root, architecture);
      before.push({architecture, build: pin(buildFile), buildId: accepted.build.buildId,
        sourceSha256: accepted.build.source.sha256, innerRunner: standard.inspectTool(setup.tools.OwnedProcessRunner)});
      collect(buildFile); collect(path.join(root, 'out', architecture, 'CMakeCache.txt'));
      collect(path.join(measurement.buildDirectory(architecture, root), 'measurement-tools.json'));
      for (const folder of attemptDirectories(architecture)) attemptsBefore.add(folder);
      snapshotArchitectures.add(architecture);
    }
    // A 64-bit caller/Runner keeps System32 PowerShell selection identical to its inspected image.
    const controllerRunner = before.find(item => item.architecture === 'x64').innerRunner;
    write('Inputs.actual.json', {schema: 'soui.release-vm-owned-inputs/v1', root, selection, builds: before, controllerRunner,
      controllerArchitecture: process.arch,
      controller: pin(__filename), node: standard.inspectTool(process.execPath), powershell: standard.inspectTool(powershell),
      desktopInnerMs: [45000, 2000], modernInnerMs: [40000, 2000], outerBudgets, releaseReady: false});
    for (const stage of selection) {
      const [architecture, kind] = stage.split(':');
      const innerRunner = before.find(item => item.architecture === architecture).innerRunner;
      standard.verifyTool(controllerRunner); standard.verifyTool(innerRunner);
      const budget = outerBudgets[kind];
      const args = ['-NoProfile', '-NonInteractive', '-ExecutionPolicy', 'Bypass', '-File',
        path.join(root, 'tools', kind === 'desktop' ? 'Test.ps1' : 'Test-Modern.ps1'),
        '-Architecture', architecture, '-Configuration', 'Release', ...(kind === 'desktop' ? ['-NativeOnly'] : [])];
      const name = architecture + '-' + kind;
      const subject = standard.inspectTool(powershell);
      attempted.add(name);
      const actual = await standard.capture(output, name, powershell, args,
        {runner: controllerRunner.path, cwd: root, ...budget});
      const expected = {executable: powershell, args, cwd: root, ...budget,
        executableSha256: subject.sha256, runnerSha256: controllerRunner.sha256, contextSha256: hash(actual.contextBytes)};
      const identity = validateStage(actual.rawReport, actual.contextBytes, actual.ready, actual.result, expected);
      const accepted = kind === 'desktop' ? evidence.verifySmoke(root, architecture, 'Release') :
        evidence.verifyModernSmoke(root, architecture, 'Release');
      need(accepted.smoke.ownedSupervision, 'Current real Owned receipt is required; legacy PID-only results are insufficient');
      stages.push({architecture, kind, ...identity, buildId: accepted.build.buildId,
        controllerRunnerSha256: controllerRunner.sha256, innerRunnerSha256: innerRunner.sha256,
        applicationRuns: kind === 'desktop' ? accepted.smoke.runs.length : 1,
        actualReport: actual.report, passed: true});
      write(name + '-Verified.actual.json', stages.at(-1));
    }
  } catch (error) { failed = {message: error.message, stack: error.stack}; }
  finally {
    // Retain complete new app attempts, including failed-attempt bytes. No old result import.
    for (const architecture of ['x86', 'x64']) {
      try {
        if (snapshotArchitectures.has(architecture))
          for (const folder of attemptDirectories(architecture)) if (!attemptsBefore.has(folder)) collectTree(folder);
        if (attempted.has(architecture + '-desktop')) collect(path.join(root, 'out', architecture, 'smoke-Release.json'));
        if (attempted.has(architecture + '-modern')) collect(path.join(root, 'out', architecture, 'modern-smoke-Release.json'));
      } catch (error) { failed ||= {message: error.message}; }
    }
    try {
      for (const item of before) if (pin(item.build.path).sha256 !== item.build.sha256) failed ||= {message: 'Original build receipt changed'};
    } catch (error) { failed ||= {message: error.message}; }
    const passed = !failed && stages.length === selection.length;
    write('Summary.actual.json', {schema: 'soui.release-vm-owned-controller/v1', root, selection, stages, failed,
      scope: selection.length === allStages.length ? 'full-eight-applications' : 'partial', passed,
      fullEightApplicationCohortExecutedInThisAttempt: selection.length === allStages.length && passed,
      releaseReady: false, productionQualified: false});
    collectTree(output);
    const rows = [...files].sort().map(pin);
    write('ExportFiles.actual.json', {schema: 'soui.release-vm-export-files/v1',
      files: rows, totalBytes: rows.reduce((sum, row) => sum + row.bytes, 0),
      source: 'New actual public smoke and outer capture bytes only; this catalog grants no test pass'});
  }
  if (failed) throw Error(failed.message);
}
main().catch(error => { console.error(error.stack); process.exitCode = 1; });
