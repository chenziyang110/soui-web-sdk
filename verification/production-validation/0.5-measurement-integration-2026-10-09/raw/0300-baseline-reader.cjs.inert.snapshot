'use strict';

// Combines two independently verified full runs. It does not infer physical
// presentation, repeatability, startup latency, or a production value verdict.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const {checkRunTraces, readLines} = require('./check-run-traces.cjs');
const {checkPagePair} = require('./check-page-pair.cjs');

function insist(condition, message) {
  if (!condition) throw new Error(message);
}

function sha(file) {
  return crypto.createHash('sha256').update(fs.readFileSync(file)).digest('hex');
}

function bundleFiles(root) {
  const output = [];
  const walk = (directory, prefix = '') => {
    for (const entry of fs.readdirSync(directory, {withFileTypes: true})) {
      const relative = prefix ? `${prefix}/${entry.name}` : entry.name;
      const file = path.join(directory, entry.name);
      if (entry.isDirectory()) walk(file, relative);
      else if (entry.isFile()) output.push({path: relative,
        bytes: fs.statSync(file).size, sha256: sha(file)});
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
  const record = JSON.parse(fs.readFileSync(path.join(root, 'run.json'), 'utf8'));
  insist(record.schema === 'soui.benchmark-single-run/v1' && record.host === host &&
    record.status === 'completed' && record.releaseReady === false &&
    record.benchmarkValid === false &&
    record.processes?.length === 3 && record.processes.every(item => item.code === 0),
  `${host}: run did not complete cleanly`);
  insist(/^page-results\/[A-Za-z0-9._-]+\.json$/.test(record.pageResult?.path),
    `${host}: unsafe page result path`);
  const pageFile = path.join(root, ...record.pageResult.path.split('/'));
  insist(fs.statSync(pageFile).size === record.pageResult.bytes &&
    sha(pageFile) === record.pageResult.sha256, `${host}: page result changed`);
  const page = JSON.parse(fs.readFileSync(pageFile, 'utf8'));
  if (record.pageBundle === 'page-bundle') {
    insist(JSON.stringify(bundleFiles(path.join(root, 'page-bundle'))) ===
      JSON.stringify(record.pageFiles), `${host}: page bundle changed`);
    insist(sha(path.join(root, 'page-bundle', 'benchmark.html')) ===
      record.pageSha256, `${host}: benchmark page changed`);
  }
  const inputFile = path.join(root, 'input.ndjson');
  const processFile = path.join(root, 'process-tree.ndjson');
  const input = readLines(inputFile);
  const process = readLines(processFile);
  const trace = checkRunTraces(input, process, record.scene);
  insist(JSON.stringify(trace) === JSON.stringify(record.trace),
    `${host}: recorded trace verdict changed`);
  const samples = process.filter(item => item.event === 'sample');
  return {
    root, record, page, input, process,
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
  const directory = path.join(electron.root, 'page-results');
  const names = fs.readdirSync(directory).filter(name => name.endsWith('-host.json'));
  if (names.length !== 1) return false;
  const metadata = JSON.parse(fs.readFileSync(path.join(directory, names[0]), 'utf8'));
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
    toolsMatched && sameMachine && hostIdentity;
  return {schema: 'soui.benchmark-pair-preflight/v1', scene: soui.record.scene,
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
