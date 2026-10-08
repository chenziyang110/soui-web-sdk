'use strict';

// A diagnostic summary of accepted paired runs. Physical presentation,
// startup, release size, and the production value verdict remain separate.
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {compare, loadRun} = require('./check-pair-runs.cjs');
const {frozenIdentity, bundleDigest} = require('./run-series.cjs');

const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const readJson = file => JSON.parse(fs.readFileSync(file, 'utf8').replace(/^\uFEFF/, ''));

function quantile(values, fraction) {
  assert(values.length > 0 && values.every(value => Number.isFinite(value) && value >= 0));
  const sorted = [...values].sort((a, b) => a - b);
  return sorted[Math.ceil(sorted.length * fraction) - 1];
}

function distribution(values) {
  return {runs: values.length, min: Math.min(...values),
    p50: quantile(values, 0.5), p95: quantile(values, 0.95),
    p99: quantile(values, 0.99), max: Math.max(...values)};
}

function validateGateCoverage(gates, pairNumber, host, startedAt, finishedAt,
  intervalMs = 5000, expectedRunnerPid) {
  const start = Date.parse(startedAt);
  const finish = Date.parse(finishedAt);
  assert(Number.isFinite(start) && Number.isFinite(finish) && finish > start,
    `Invalid run interval for pair ${pairNumber} ${host}`);
  const tolerance = Math.max(15000, intervalMs * 3);
  assert(Number.isSafeInteger(expectedRunnerPid) && expectedRunnerPid > 0,
    `Missing recorded runner PID for pair ${pairNumber} ${host}`);
  const matching = gates.filter(gate => gate.pairNumber === pairNumber && gate.host === host);
  assert(matching.every(gate => gate.ownRunPid === expectedRunnerPid),
    `Live gate runner PID differs from run record for pair ${pairNumber} ${host}`);
  const times = matching.map(gate => Date.parse(gate.checkedAt));
  assert(times.length > 0 && times.every(Number.isFinite),
    `Missing timed live gate for pair ${pairNumber} ${host}`);
  assert(times[0] <= start + tolerance && times[0] >= start - tolerance,
    `Live gate did not cover run start for pair ${pairNumber} ${host}`);
  assert(times[times.length - 1] >= finish - tolerance &&
    times[times.length - 1] <= finish + tolerance,
  `Live gate did not cover run finish for pair ${pairNumber} ${host}`);
  assert(times.some(time => time >= start && time <= finish),
    `No live gate inside measured run for pair ${pairNumber} ${host}`);
  for (let index = 1; index < times.length; ++index)
    assert(times[index] >= times[index - 1] &&
      times[index] - times[index - 1] <= tolerance,
    `Live gate gap for pair ${pairNumber} ${host}`);
  return {pairNumber, host, samples: times.length,
    firstAt: new Date(times[0]).toISOString(),
    lastAt: new Date(times[times.length - 1]).toISOString()};
}

function validateRunChronology(series, runTimes) {
  const seriesStart = Date.parse(series.startedAt);
  const seriesFinish = Date.parse(series.finishedAt);
  assert(Number.isFinite(seriesStart) && Number.isFinite(seriesFinish) &&
    seriesFinish > seriesStart, 'Run chronology has an invalid series interval');
  assert.equal(runTimes.length, series.plannedPairs * 2,
    'Run chronology requires exactly two runs per pair');
  const byKey = new Map();
  for (const run of runTimes) {
    const key = `${run.pairNumber}:${run.host}`;
    assert(!byKey.has(key), `Run chronology contains a duplicate ${key}`);
    byKey.set(key, run);
  }
  const ordered = [];
  let previousFinish = seriesStart;
  for (let index = 0; index < series.plannedPairs; ++index) {
    for (const host of series.pairs[index].order) {
      const pairNumber = index + 1;
      const run = byKey.get(`${pairNumber}:${host}`);
      assert(run, `Run chronology is missing pair ${pairNumber} ${host}`);
      const start = Date.parse(run.startedAt);
      const finish = Date.parse(run.finishedAt);
      assert(Number.isFinite(start) && Number.isFinite(finish) &&
        start >= previousFinish && finish > start && finish <= seriesFinish,
      `Run chronology overlaps or reverses pair ${pairNumber} ${host}`);
      previousFinish = finish;
      ordered.push({pairNumber, host, startedAt: run.startedAt,
        finishedAt: run.finishedAt});
    }
  }
  return ordered;
}

function validatePreflightChronology(series, orderedRuns) {
  assert.equal(series.checks.length, 1 + orderedRuns.length * 2,
    'Preflight chronology requires a start gate and two gates per run');
  const seriesStart = Date.parse(series.startedAt);
  const seriesFinish = Date.parse(series.finishedAt);
  let previousGate = Date.parse(series.checks[0].checkedAt);
  const freshnessMs = 15000;
  assert(Number.isFinite(previousGate) && previousGate <= seriesStart &&
    seriesStart - previousGate <= freshnessMs,
    'Preflight chronology has an invalid start gate');
  const checks = [];
  for (let index = 0; index < orderedRuns.length; ++index) {
    const run = orderedRuns[index];
    const before = Date.parse(series.checks[index * 2 + 1].checkedAt);
    const after = Date.parse(series.checks[index * 2 + 2].checkedAt);
    const start = Date.parse(run.startedAt);
    const finish = Date.parse(run.finishedAt);
    assert(Number.isFinite(before) && Number.isFinite(after) &&
      before >= previousGate && before <= start && start - before <= freshnessMs &&
      after >= finish && after - finish <= freshnessMs && after <= seriesFinish,
    `Preflight chronology does not bracket pair ${run.pairNumber} ${run.host}`);
    previousGate = after;
    checks.push({pairNumber: run.pairNumber, host: run.host,
      beforeAt: series.checks[index * 2 + 1].checkedAt,
      afterAt: series.checks[index * 2 + 2].checkedAt});
  }
  return checks;
}

function assertRunnerBinding(pair, host, record, gates) {
  const runner = pair.runnerProcesses?.[host];
  assert(Number.isSafeInteger(runner?.pid) && runner.pid > 0,
    'Series runner PID is missing');
  assert.equal(record.runnerPid, runner.pid,
    'Single-run process differs from the series runner');
  const legacyDate = typeof runner.createdAt === 'string'
    ? /^\/Date\(([0-9]+)(?:[+-][0-9]+)?\)\/$/.exec(runner.createdAt) : null;
  assert(typeof runner.createdAt === 'string' &&
    (Number.isFinite(Date.parse(runner.createdAt)) ||
      (legacyDate && Number.isSafeInteger(Number(legacyDate[1])) &&
        Number(legacyDate[1]) > 0)),
  'Series runner creation identity is missing');
  const samples = gates.filter(gate => gate.pairNumber === pair.number && gate.host === host);
  assert(samples.length > 0, 'Series runner has no live gate samples');
  for (const sample of samples) {
    assert.equal(sample.ownRunPid, runner.pid,
      'Live gate PID differs from the series runner');
    assert.equal(sample.ownRunCreatedAt, runner.createdAt,
      'Live gate creation identity differs from the series runner');
  }
}

function summarizeSeriesRecord(series, gates, reports) {
  assert.equal(series.schema, 'soui.benchmark-series/v1');
  assert(['S01', 'S02', 'S04', 'S05'].includes(series.scene));
  assert.equal(series.status, 'preflight-completed');
  assert.equal(series.benchmarkValid, false);
  assert.equal(series.releaseReady, false);
  assert(Number.isInteger(series.plannedPairs) && series.plannedPairs >= 5 &&
    series.plannedPairs <= 20, 'At least five paired runs are required');
  assert.equal(series.completedPairs, series.plannedPairs);
  assert.equal(series.pairs.length, series.plannedPairs);
  assert.equal(reports.length, series.plannedPairs);
  assert.equal(series.order, 'alternating-soui-electron-and-electron-soui');
  assert.equal(series.liveInterferenceGate?.intervalMs, 5000);
  assert.equal(series.liveInterferenceGate?.path, 'live-interference-gates.ndjson');
  assert.equal(series.checks?.length, 1 + series.plannedPairs * 4,
    'All start, before-run and after-run gates are required');
  for (const check of series.checks) {
    assert(Number.isFinite(Date.parse(check.checkedAt)), 'Invalid preflight gate time');
    assert.deepEqual(check.competingProcesses, [],
      'External workload invalidates the series');
  }
  const gateCounts = new Map();
  const gatePids = new Map();
  for (const gate of gates) {
    assert(Number.isFinite(Date.parse(gate.checkedAt)), 'Invalid gate time');
    assert(Number.isInteger(gate.pairNumber) && gate.pairNumber >= 1 &&
      gate.pairNumber <= series.plannedPairs);
    assert(['soui', 'electron'].includes(gate.host));
    assert(Number.isSafeInteger(gate.ownRunPid) && gate.ownRunPid > 0,
      'Live gate is not bound to a run process');
    assert.equal(gate.rootPresent, true, 'Live gate did not observe its run process');
    assert.deepEqual(gate.competingProcesses, [], 'External workload invalidates the series');
    assert(!gate.gateError && !gate.cleanupReason && !gate.cleanupCompletenessUnverified,
      'Gate failure or forced cleanup invalidates the series');
    const key = `${gate.pairNumber}:${gate.host}`;
    if (gatePids.has(key))
      assert.equal(gate.ownRunPid, gatePids.get(key), 'Run PID changed within a live gate');
    else gatePids.set(key, gate.ownRunPid);
    gateCounts.set(key, (gateCounts.get(key) || 0) + 1);
  }
  const runIds = new Set();
  const values = {soui: {pageDurationMs: [], inputDurationMs: [],
    privateBytesP95: [], cpuPercentP95: []},
  electron: {pageDurationMs: [], inputDurationMs: [],
    privateBytesP95: [], cpuPercentP95: []}};
  for (let index = 0; index < series.plannedPairs; ++index) {
    const pair = series.pairs[index];
    const report = reports[index];
    const number = index + 1;
    const order = index % 2 === 0 ? ['soui', 'electron'] : ['electron', 'soui'];
    assert.equal(pair.number, number);
    assert.deepEqual(pair.order, order, 'Order must alternate by pair');
    assert.equal(pair.preflightAccepted, true, 'Only accepted pairs can be summarized');
    assert.equal(pair.runs.soui, `pair-${number}-soui`);
    assert.equal(pair.runs.electron, `pair-${number}-electron`);
    assert.equal(report.schema, 'soui.benchmark-pair-preflight/v1');
    assert.equal(report.scene, series.scene);
    assert.equal(report.preflightAccepted, true, 'Only accepted pairs can be summarized');
    assert.equal(report.benchmarkValid, false);
    assert.equal(report.releaseReady, false);
    assert.equal(report.runs.length, 2);
    for (const host of ['soui', 'electron']) {
      assert((gateCounts.get(`${number}:${host}`) || 0) >= 1,
        `Missing live gate for pair ${number} ${host}`);
      const run = report.runs.find(item => item.host === host);
      assert(run && typeof run.runId === 'string' && run.runId.length > 0);
      assert(!runIds.has(run.runId), 'Run IDs must be distinct');
      runIds.add(run.runId);
      assert(Number.isInteger(run.resources.samples) && run.resources.samples >= 2);
      for (const metric of Object.keys(values[host])) {
        const value = metric in run.resources ? run.resources[metric] : run[metric];
        assert(Number.isFinite(value) && value >= 0, `${host} ${metric} is missing`);
        values[host][metric].push(value);
      }
    }
  }
  const metrics = Object.fromEntries(['soui', 'electron'].map(host => [host,
    Object.fromEntries(Object.entries(values[host]).map(([name, samples]) =>
      [name, distribution(samples)]))]));
  assert(metrics.electron.privateBytesP95.p50 > 0,
    'Electron private-memory reference must be positive');
  return {schema: 'soui.benchmark-series-diagnostic/v1',
    scene: series.scene, acceptedPairs: series.plannedPairs,
    order: series.order, liveGateSamples: gates.length,
    metrics, candidateRatios: {
      privateBytesP95P50: metrics.soui.privateBytesP95.p50 /
        metrics.electron.privateBytesP95.p50,
      cpuPercentP95P50: metrics.electron.cpuPercentP95.p50 > 0
        ? metrics.soui.cpuPercentP95.p50 / metrics.electron.cpuPercentP95.p50 : null},
    metricMeaning: 'Quantiles across per-run summaries; not pooled frame or input latency',
    physicalPresentationMeasured: false, startupMeasured: false,
    packageSizeMeasured: false, benchmarkValid: false, releaseReady: false,
    remainingEvidence: ['physical-input-to-presentation', 'cold-and-warm-startup',
      'complete-runtime-package-size', 'target-platform-and-product-workload',
      'security-and-license-release-gates']};
}

function summarizeDirectory(root) {
  const seriesPath = path.join(root, 'series.json');
  const gatesPath = path.join(root, 'live-interference-gates.ndjson');
  const series = readJson(seriesPath);
  assert.equal(series.pageBundle, 'frozen-page');
  assert.equal(bundleDigest(path.join(root, 'frozen-page')), series.pageBundleSha256,
    'Frozen page bundle changed after the series');
  const gates = fs.readFileSync(gatesPath, 'utf8').trim().split(/\r?\n/)
    .filter(Boolean).map(JSON.parse);
  const reports = [];
  const evidence = [];
  const runTimes = [];
  let frozen = null;
  for (let number = 1; number <= series.plannedPairs; ++number) {
    const runs = {};
    for (const host of ['soui', 'electron'])
      runs[host] = loadRun(path.join(root, `pair-${number}-${host}`), host);
    for (const host of ['soui', 'electron']) {
      assertRunnerBinding(series.pairs[number - 1], host, runs[host].record, gates);
      runTimes.push({pairNumber: number, host,
        startedAt: runs[host].record.startedAt,
        finishedAt: runs[host].record.finishedAt,
        runnerPid: runs[host].record.runnerPid});
    }
    const identity = {soui: frozenIdentity(runs.soui),
      electron: frozenIdentity(runs.electron)};
    if (frozen) assert.deepEqual(identity, frozen, 'Frozen identity changed across pairs');
    else frozen = identity;
    assert.deepEqual(identity, series.fixedIdentities, 'Series identity record differs');
    const report = compare(runs.soui, runs.electron);
    const reportPath = path.join(root, `pair-${number}-preflight.json`);
    const stored = readJson(reportPath);
    assert.deepEqual(stored, report, `Pair ${number} preflight changed`);
    reports.push(report);
    evidence.push({pair: number, preflightSha256: sha(fs.readFileSync(reportPath))});
  }
  const summary = summarizeSeriesRecord(series, gates, reports);
  const runChronology = validateRunChronology(series, runTimes);
  const preflightChronology = validatePreflightChronology(series, runChronology);
  const liveGateCoverage = runTimes.map(run => validateGateCoverage(gates,
    run.pairNumber, run.host, run.startedAt, run.finishedAt,
    series.liveInterferenceGate.intervalMs, run.runnerPid));
  return {...summary, sourceIdentity: frozen,
    runChronology, preflightChronology, liveGateCoverage,
    sourceSha256: {series: sha(fs.readFileSync(seriesPath)),
      liveGate: sha(fs.readFileSync(gatesPath))}, pairs: evidence};
}

if (require.main === module) {
  try {
    if (process.argv.length !== 3 || !path.isAbsolute(process.argv[2]))
      throw new Error('Usage: node summarize-series.cjs ABS_COMPLETED_SERIES_DIRECTORY');
    const root = fs.realpathSync(process.argv[2]);
    const summary = summarizeDirectory(root);
    fs.writeFileSync(path.join(root, 'series-diagnostic-summary.json'),
      JSON.stringify(summary, null, 2) + '\n', {flag: 'wx'});
    process.stdout.write(`${JSON.stringify({acceptedPairs: summary.acceptedPairs,
      benchmarkValid: false, releaseReady: false})}\n`);
  } catch (error) {
    console.error(error.message);
    process.exitCode = 1;
  }
}

module.exports = {summarizeSeriesRecord, summarizeDirectory, validateGateCoverage,
  validateRunChronology, validatePreflightChronology, assertRunnerBinding};
