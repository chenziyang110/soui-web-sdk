'use strict';

const assert = require('node:assert/strict');
const {test} = require('node:test');
const {summarizeSeriesRecord, validateGateCoverage, validateRunChronology,
  validatePreflightChronology, assertRunnerBinding} =
  require('../../benchmarks/protocol/summarize-series.cjs');

function acceptedSeries(count = 5) {
  const series = {schema: 'soui.benchmark-series/v1', scene: 'S02',
    status: 'preflight-completed', benchmarkValid: false, releaseReady: false,
    plannedPairs: count, completedPairs: count,
    order: 'alternating-soui-electron-and-electron-soui',
    liveInterferenceGate: {path: 'live-interference-gates.ndjson', intervalMs: 5000},
    checks: Array.from({length: 1 + count * 4}, () => ({
      checkedAt: '2026-09-28T00:00:00.000Z', competingProcesses: []})),
    pairs: []};
  const gates = [];
  const reports = [];
  for (let number = 1; number <= count; ++number) {
    series.pairs.push({number, order: number % 2
      ? ['soui', 'electron'] : ['electron', 'soui'],
    preflightAccepted: true,
    runs: {soui: `pair-${number}-soui`, electron: `pair-${number}-electron`}});
    const runs = [];
    for (const host of ['soui', 'electron']) {
      gates.push({checkedAt: '2026-09-28T00:00:00.000Z', pairNumber: number,
        host, ownRunPid: number * 10 + (host === 'soui' ? 1 : 2),
        rootPresent: true,
        competingProcesses: []});
      runs.push({host, runId: `${number}-${host}`,
        pageDurationMs: 60000 + number * 100,
        inputDurationMs: 61000 + number * 100,
        resources: {samples: 61,
          privateBytesP95: (host === 'soui' ? 30 : 90) + number * 10,
          cpuPercentP95: (host === 'soui' ? 3 : 8) + number}});
    }
    reports.push({schema: 'soui.benchmark-pair-preflight/v1', scene: 'S02',
      preflightAccepted: true, benchmarkValid: false, releaseReady: false, runs});
  }
  return {series, gates, reports};
}

test('five alternating accepted pairs produce diagnostics without a value verdict', () => {
  const input = acceptedSeries();
  const summary = summarizeSeriesRecord(input.series, input.gates, input.reports);
  assert.equal(summary.acceptedPairs, 5);
  assert.equal(summary.metrics.soui.privateBytesP95.p50, 60);
  assert.equal(summary.metrics.electron.privateBytesP95.p50, 120);
  assert.equal(summary.candidateRatios.privateBytesP95P50, 0.5);
  assert.equal(summary.metrics.soui.privateBytesP95.p99, 80);
  assert.equal(summary.physicalPresentationMeasured, false);
  assert.equal(summary.benchmarkValid, false);
  assert.equal(summary.releaseReady, false);
});

test('four pairs cannot be summarized as the five-pair acceptance input', () => {
  const input = acceptedSeries(4);
  assert.throws(() => summarizeSeriesRecord(input.series, input.gates, input.reports),
    /At least five paired runs/);
});

test('a missing or contaminated live gate invalidates all candidate ratios', () => {
  const input = acceptedSeries();
  input.gates.pop();
  assert.throws(() => summarizeSeriesRecord(input.series, input.gates, input.reports),
    /Missing live gate/);
  const contaminated = acceptedSeries();
  contaminated.gates[4].competingProcesses = [{pid: 123, name: 'qemu-system-i386.exe'}];
  assert.throws(() => summarizeSeriesRecord(contaminated.series,
    contaminated.gates, contaminated.reports), /External workload/);
  const preflight = acceptedSeries();
  preflight.series.checks[3].competingProcesses = [{pid: 123,
    name: 'qemu-system-x86_64.exe'}];
  assert.throws(() => summarizeSeriesRecord(preflight.series,
    preflight.gates, preflight.reports), /External workload/);
});

test('order drift and duplicate run identity are refused', () => {
  const order = acceptedSeries();
  order.series.pairs[1].order.reverse();
  assert.throws(() => summarizeSeriesRecord(order.series, order.gates, order.reports),
    /Order must alternate/);
  const duplicate = acceptedSeries();
  duplicate.reports[4].runs[0].runId = duplicate.reports[0].runs[0].runId;
  assert.throws(() => summarizeSeriesRecord(duplicate.series,
    duplicate.gates, duplicate.reports), /Run IDs must be distinct/);
  const binding = acceptedSeries();
  binding.gates[0].ownRunPid = undefined;
  assert.throws(() => summarizeSeriesRecord(binding.series,
    binding.gates, binding.reports), /not bound to a run process/);
  const missingRunner = acceptedSeries();
  missingRunner.gates[0].rootPresent = false;
  assert.throws(() => summarizeSeriesRecord(missingRunner.series,
    missingRunner.gates, missingRunner.reports), /did not observe its run process/);
});

test('a rejected pair or forced cleanup cannot contribute to a summary', () => {
  const rejected = acceptedSeries();
  rejected.reports[2].preflightAccepted = false;
  assert.throws(() => summarizeSeriesRecord(rejected.series,
    rejected.gates, rejected.reports), /Only accepted pairs/);
  const cleanup = acceptedSeries();
  cleanup.gates[0].cleanupReason = 'gate-error';
  assert.throws(() => summarizeSeriesRecord(cleanup.series,
    cleanup.gates, cleanup.reports), /Gate failure/);
  const zeroReference = acceptedSeries();
  for (const report of zeroReference.reports)
    report.runs.find(run => run.host === 'electron').resources.privateBytesP95 = 0;
  assert.throws(() => summarizeSeriesRecord(zeroReference.series,
    zeroReference.gates, zeroReference.reports), /reference must be positive/);
});

test('live gate timestamps must cover a full run without sampling gaps', () => {
  const start = '2026-09-28T00:00:00.000Z';
  const finish = '2026-09-28T00:01:00.000Z';
  const gates = Array.from({length: 13}, (_, index) => ({pairNumber: 1,
    host: 'soui', ownRunPid: 123,
    checkedAt: new Date(Date.parse(start) + index * 5000).toISOString()}));
  const coverage = validateGateCoverage(gates, 1, 'soui', start, finish, 5000, 123);
  assert.equal(coverage.samples, 13);
  assert.equal(coverage.lastAt, finish);
  assert.throws(() => validateGateCoverage(gates, 1, 'soui', start, finish, 5000, 999),
    /runner PID/);
  assert.throws(() => validateGateCoverage(gates.filter((_, index) =>
    index <= 3 || index >= 9), 1, 'soui', start, finish, 5000, 123), /Live gate gap/);
  assert.throws(() => validateGateCoverage(gates.slice(6), 1, 'soui', start, finish, 5000, 123),
    /did not cover run start/);
  assert.throws(() => validateGateCoverage(gates.slice(0, 7), 1, 'soui', start, finish, 5000, 123),
    /did not cover run finish/);
  assert.throws(() => validateGateCoverage(gates.slice(0, 1), 1, 'soui', start, finish, 5000, 123),
    /did not cover run finish/);
  const outsideShortRun = [
    {pairNumber: 1, host: 'soui', ownRunPid: 123,
      checkedAt: '2026-09-28T00:00:06.000Z'},
    {pairNumber: 1, host: 'soui', ownRunPid: 123,
      checkedAt: '2026-09-28T00:00:18.000Z'},
  ];
  assert.throws(() => validateGateCoverage(outsideShortRun, 1, 'soui',
    '2026-09-28T00:00:10.000Z', '2026-09-28T00:00:14.000Z', 5000, 123),
  /inside measured run/);
});

test('real run times follow the declared alternating order without overlap', () => {
  const {series} = acceptedSeries();
  series.startedAt = '2026-09-28T00:00:00.000Z';
  series.finishedAt = '2026-09-28T00:10:01.000Z';
  const runTimes = [];
  let nextStart = Date.parse(series.startedAt) + 1000;
  for (const pair of series.pairs) {
    for (const host of pair.order) {
      runTimes.push({pairNumber: pair.number, host,
        startedAt: new Date(nextStart).toISOString(),
        finishedAt: new Date(nextStart + 60000).toISOString()});
      nextStart += 60000;
    }
  }
  assert.equal(validateRunChronology(series, runTimes).length, 10);
  const swapped = structuredClone(runTimes);
  [swapped[0].startedAt, swapped[1].startedAt] =
    [swapped[1].startedAt, swapped[0].startedAt];
  assert.throws(() => validateRunChronology(series, swapped), /Run chronology/);
  const overlap = structuredClone(runTimes);
  overlap[1].startedAt = new Date(Date.parse(overlap[0].finishedAt) - 1).toISOString();
  assert.throws(() => validateRunChronology(series, overlap), /Run chronology/);
  assert.throws(() => validateRunChronology(series, runTimes.slice(1)),
    /Run chronology/);
});

test('each before and after preflight gate brackets its actual run', () => {
  const {series} = acceptedSeries();
  series.startedAt = '2026-09-28T00:00:01.000Z';
  series.finishedAt = '2026-09-28T00:10:31.000Z';
  const runTimes = [];
  const checks = [{checkedAt: '2026-09-28T00:00:00.000Z',
    competingProcesses: []}];
  let nextStart = Date.parse(series.startedAt) + 1000;
  for (const pair of series.pairs) {
    for (const host of pair.order) {
      const startedAt = new Date(nextStart).toISOString();
      const finishedAt = new Date(nextStart + 60000).toISOString();
      runTimes.push({pairNumber: pair.number, host, startedAt, finishedAt});
      checks.push({checkedAt: new Date(nextStart - 1000).toISOString(),
        competingProcesses: []});
      checks.push({checkedAt: new Date(nextStart + 60001).toISOString(),
        competingProcesses: []});
      nextStart += 63000;
    }
  }
  series.checks = checks;
  assert.equal(validatePreflightChronology(series, runTimes).length, 10);
  const staleBefore = structuredClone(series);
  staleBefore.checks[3].checkedAt = staleBefore.checks[1].checkedAt;
  assert.throws(() => validatePreflightChronology(staleBefore, runTimes),
    /Preflight chronology/);
  const earlyAfter = structuredClone(series);
  earlyAfter.checks[2].checkedAt = runTimes[0].startedAt;
  assert.throws(() => validatePreflightChronology(earlyAfter, runTimes),
    /Preflight chronology/);
});

test('preflight gates too far from a measured run are rejected', () => {
  const run = [{pairNumber: 1, host: 'soui',
    startedAt: '2026-09-28T00:01:00.000Z',
    finishedAt: '2026-09-28T00:01:10.000Z'}];
  const series = {startedAt: '2026-09-28T00:00:00.000Z',
    finishedAt: '2026-09-28T00:02:00.000Z', checks: [
      {checkedAt: '2026-09-28T00:00:00.000Z'},
      {checkedAt: '2026-09-28T00:00:01.000Z'},
      {checkedAt: '2026-09-28T00:01:11.000Z'},
    ]};
  assert.throws(() => validatePreflightChronology(series, run),
    /Preflight chronology/);
  series.checks[1].checkedAt = '2026-09-28T00:00:59.000Z';
  series.checks[2].checkedAt = '2026-09-28T00:01:59.000Z';
  assert.throws(() => validatePreflightChronology(series, run),
    /Preflight chronology/);
  series.checks[2].checkedAt = '2026-09-28T00:01:11.000Z';
  series.startedAt = '2026-09-28T00:00:30.000Z';
  assert.throws(() => validatePreflightChronology(series, run),
    /Preflight chronology/);
});

test('series, single-run and live gate must identify the same runner', () => {
  const pair = {number: 1, runnerProcesses: {
    soui: {pid: 41, createdAt: '2026-09-28T00:00:00.000Z'}}};
  const record = {runnerPid: 41};
  const gates = [{pairNumber: 1, host: 'soui', ownRunPid: 41,
    ownRunCreatedAt: '2026-09-28T00:00:00.000Z'}];
  assert.doesNotThrow(() => assertRunnerBinding(pair, 'soui', record, gates));
  assert.throws(() => assertRunnerBinding(pair, 'soui', {runnerPid: 42}, gates),
    /Single-run process differs/);
  assert.throws(() => assertRunnerBinding(pair, 'soui', record,
    [{...gates[0], ownRunPid: 42}]), /Live gate PID differs/);
  assert.throws(() => assertRunnerBinding(pair, 'soui', record,
    [{...gates[0], ownRunCreatedAt: '2026-09-28T00:01:00.000Z'}]),
  /Live gate creation identity differs/);
  const powershellDate = '/Date(1790562756903)/';
  assert.doesNotThrow(() => assertRunnerBinding(
    {number: 1, runnerProcesses: {soui: {pid: 41, createdAt: powershellDate}}},
    'soui', record, [{...gates[0], ownRunCreatedAt: powershellDate}]));
  const powershellDateWithOffset = '/Date(1790562756903+0800)/';
  assert.doesNotThrow(() => assertRunnerBinding(
    {number: 1, runnerProcesses: {soui: {pid: 41,
      createdAt: powershellDateWithOffset}}}, 'soui', record,
    [{...gates[0], ownRunCreatedAt: powershellDateWithOffset}]));
});
