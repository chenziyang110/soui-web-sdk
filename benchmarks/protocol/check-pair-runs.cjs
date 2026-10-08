'use strict';

// Combines two independently verified full runs. It does not infer physical
// presentation, repeatability, startup latency, or a production value verdict.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {isDeepStrictEqual} = require('node:util');
const {checkRunTraces} = require('./check-run-traces.cjs');
const {checkPagePair} = require('./check-page-pair.cjs');
const {runFile, runDirectory, readRunFile, readRunJson, readRunLines, readOwnedRun} = require('./owned-trace-subject.cjs');

function insist(condition, message) {
  if (!condition) throw new Error(message);
}

function sha(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function bundleFiles(root, runRoot) {
  const output = [];
  const walk = (directory, prefix = '') => {
    for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
      const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) walk(runDirectory(runRoot, 'page-bundle/' + relative), relative);
      else if (entry.isFile()) {
        const bytes = readRunFile(runRoot, 'page-bundle/' + relative);
        output.push({path: relative, bytes: bytes.length,
          sha256: crypto.createHash('sha256').update(bytes).digest('hex')});
      }
      else throw new Error(`Unsupported page bundle entry: ${relative}`);
    }
  };
  walk(root);
  return output.sort((left, right) => left.path.localeCompare(right.path));
}

function quantile(values, fraction) {
  const sorted = values.filter(Number.isFinite).sort((left, right) => left - right);
  return sorted.length ? sorted[Math.ceil(sorted.length * fraction) - 1] : null;
}

function loadRun(directory, host) {
  const root = fs.realpathSync(directory);
  const record = readRunJson(root, 'run.json');
  const exact = record.schema === 'soui.benchmark-single-run/v2';
  insist((exact || record.schema === 'soui.benchmark-single-run/v1') && record.host === host &&
    record.status === 'completed' && record.releaseReady === false &&
    record.benchmarkValid === false &&
    record.processes?.length === 3 && record.processes.every(item => item.code === 0),
  `${host}: run did not complete cleanly`);
  insist(/^page-results\/[A-Za-z0-9._-]+\.json$/.test(record.pageResult?.path),
    `${host}: unsafe page result path`);
  const pageFile = runFile(root, record.pageResult.path);
  insist(fs.statSync(pageFile).size === record.pageResult.bytes &&
    sha(pageFile) === record.pageResult.sha256, `${host}: page result changed`);
  const page = readRunJson(root, record.pageResult.path);
  if (record.pageBundle === 'page-bundle') {
    insist(JSON.stringify(bundleFiles(runDirectory(root, 'page-bundle'), root)) ===
      JSON.stringify(record.pageFiles), `${host}: page bundle changed`);
    insist(sha(runFile(root, 'page-bundle/benchmark.html')) ===
      record.pageSha256, `${host}: benchmark page changed`);
  }
  const inputFile = runFile(root, 'input.ndjson');
  const processFile = runFile(root, 'process-tree.ndjson');
  const input = readRunLines(root, 'input.ndjson');
  const process = readRunLines(root, 'process-tree.ndjson');
  const owned = exact ? readOwnedRun(root, record) : null;
  const trace = checkRunTraces(input, process, record.scene, owned ? {ownedSubject:owned.subject} : {});
  if (exact) {
    insist(trace.exactFileTimeBound === true && isDeepStrictEqual(trace, record.trace),
      `${host}: recorded exact trace verdict changed`);
    insist(isDeepStrictEqual(record.traceIdentity,
      {pidBound:true, creationComparison:'exact-native-filetime', exactFileTimeBound:true}),
    `${host}: recorded trace identity changed`);
  } else {
    insist(!trace.exactFileTimePair && record.trace?.schema === 'soui.benchmark-run-traces/v1',
      `${host}: legacy run cannot claim an exact trace`);
    const originalKeys = ['schema', 'scene', 'hostPid', 'hostCreationTime100ns', 'inputActions',
      'processSamples', 'inputDurationMs', 'processDurationMs', 'inputOffsetMs',
      'traceIntegrityAccepted', 'physicalPresentation', 'performanceGate', 'releaseReady'];
    const projection = Object.fromEntries(originalKeys.map(key => [key,
      key === 'schema' ? 'soui.benchmark-run-traces/v1' : trace[key]]));
    insist(isDeepStrictEqual(projection, record.trace), `${host}: recorded legacy trace verdict changed`);
  }
  const samples = process.filter(item => item.event === 'sample');
  return {
    root, record, page, input, process, trace,
    identityBinding: {version:exact ? 2 : 1, exactFileTimeBound:trace.exactFileTimeBound,
      naturalCompletion:owned?.naturalCompletion ?? false, legacyUnbound:!exact},
    hashes: {run: sha(path.join(root, 'run.json')), page: sha(pageFile),
      input: sha(inputFile), processTree: sha(processFile)},
    resources: {samples: samples.length,
      privateBytesP50: quantile(samples.map(item => item.privateBytes), 0.5),
      privateBytesP95: quantile(samples.map(item => item.privateBytes), 0.95),
      cpuPercentP50: quantile(samples.map(item => item.cpuPercent), 0.5),
      cpuPercentP95: quantile(samples.map(item => item.cpuPercent), 0.95)},
  };
}

function checkElectronMetadata(electron) {
  const directory = runDirectory(electron.root, 'page-results');
  const names = fs.readdirSync(directory).filter(name => name.endsWith('-host.json'));
  if (names.length !== 1) return false;
  const metadata = readRunJson(electron.root, 'page-results/' + names[0]);
  return metadata.schema === 'soui.benchmark-electron-run/v1' &&
    metadata.pageSha256 === electron.record.pageSha256 &&
    metadata.versions?.electron === '44.4.4' &&
    metadata.security?.sandbox === true &&
    metadata.security?.contextIsolation === true &&
    metadata.security?.nodeIntegration === false &&
    metadata.security?.webSecurity === true;
}

function compare(soui, electron) {
  insist(soui.record.scene === electron.record.scene, 'Scene identity differs');
  insist(soui.identityBinding.version === electron.identityBinding.version, 'Mixed legacy/exact runs cannot form a formal pair');
  const exact = soui.identityBinding.version === 2;
  const exactFileTimeBound = exact && soui.identityBinding.exactFileTimeBound &&
    electron.identityBinding.exactFileTimeBound && soui.identityBinding.naturalCompletion &&
    electron.identityBinding.naturalCompletion;
  const page = checkPagePair(soui.page, electron.page);
  const souiActions = soui.input.filter(item => item.event === 'input').map(item => item.action);
  const electronActions = electron.input.filter(item => item.event === 'input').map(item => item.action);
  const inputMatched = JSON.stringify(souiActions) === JSON.stringify(electronActions) &&
    Math.abs(soui.record.trace.inputDurationMs - electron.record.trace.inputDurationMs) <= 5000;
  const samePageBundle = Array.isArray(soui.record.pageFiles) &&
    soui.record.pageBundle === 'page-bundle' &&
    electron.record.pageBundle === 'page-bundle' &&
    soui.record.pageFiles.length > 0 &&
    JSON.stringify(soui.record.pageFiles) === JSON.stringify(electron.record.pageFiles) &&
    soui.record.pageSha256 === electron.record.pageSha256;
  const toolsMatched = soui.record.tools?.samplerSha256 === electron.record.tools?.samplerSha256 &&
    soui.record.tools?.inputSha256 === electron.record.tools?.inputSha256;
  const sameMachine = soui.record.machine?.hostname &&
    JSON.stringify(soui.record.machine) === JSON.stringify(electron.record.machine);
  const hostIdentity = soui.record.identity?.kind === 'sdk-library' &&
    /^[a-f0-9]{64}$/.test(soui.record.identity.sha256) &&
    electron.record.identity?.kind === 'electron-lockfile' &&
    /^[a-f0-9]{64}$/.test(electron.record.identity.sha256) &&
    Array.isArray(electron.record.dependency?.appFiles) &&
    electron.record.dependency.appFiles.length === 4 &&
    electron.record.dependency.appFiles.find(item => item.path === 'package-lock.json')?.sha256 ===
      electron.record.identity.sha256 &&
    checkElectronMetadata(electron);
  const preflightAccepted = page.pagePairAccepted && inputMatched && samePageBundle &&
    toolsMatched && sameMachine && hostIdentity && (!exact || exactFileTimeBound);
  return {schema: exact ? 'soui.benchmark-pair-preflight/v2' : 'soui.benchmark-pair-preflight/v1', scene: soui.record.scene,
    preflightScope:exact ? 'exact-owned-subject' : 'legacy-diagnostic-unbound',
    exactFileTimeBound:Boolean(exactFileTimeBound),
    preflightAccepted, checks: {pagePair: page.pagePairAccepted,
      inputMatched, samePageBundle, toolsMatched, sameMachine: Boolean(sameMachine), hostIdentity,
      bothTraceIntegrityAccepted: true},
    runs: [soui, electron].map(run => ({host: run.record.host,
      runId: run.record.runId, pageDurationMs: run.page.timing.durationMs,
      inputDurationMs: run.record.trace.inputDurationMs,
      resources: run.resources, rawSha256: run.hashes})),
    remainingEvidence: ['physical-presentation', 'startup-samples',
      'five-full-runs-per-scene-and-alternated-order', 'target-system-and-product-workload'],
    performanceGate: 'not-evaluated', benchmarkValid: false, releaseReady: false};
}

if (require.main === module) {
  try {
    if (process.argv.length !== 4)
      throw new Error('Usage: node check-pair-runs.cjs SOUI_RUN_DIRECTORY ELECTRON_RUN_DIRECTORY');
    const report = compare(loadRun(process.argv[2], 'soui'),
      loadRun(process.argv[3], 'electron'));
    process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
    if (!report.preflightAccepted) process.exitCode = 2;
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}

module.exports = {compare, loadRun};
