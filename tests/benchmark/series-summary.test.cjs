'use strict';

const assert = require('node:assert/strict');
const {test} = require('node:test');
const {summarizeSeriesRecord, assertLiveGateCoverage, assertRunnerBinding} =
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

test('live gate must cover each real run without a missing interval', () => {
  const input = acceptedSeries();
  const start = Date.parse('2026-09-28T00:00:00.000Z');
  const periods = [];
  const gates = [];
  for (let number = 1; number <= 5; ++number) {
    for (const host of ['soui', 'electron']) {
      periods.push({pairNumber: number, host,
        startedAt: new Date(start).toISOString(),
        finishedAt: new Date(start + 60000).toISOString()});
      for (let elapsed = 0; elapsed <= 60000; elapsed += 5000)
        gates.push({pairNumber: number, host,
          checkedAt: new Date(start + elapsed).toISOString()});
    }
  }
  assert.doesNotThrow(() => assertLiveGateCoverage(input.series, gates, periods));
  const truncated = gates.filter(gate => gate.pairNumber !== 1 ||
    gate.host !== 'soui' || Date.parse(gate.checkedAt) < start + 15000 ||
    Date.parse(gate.checkedAt) > start + 45000);
  assert.throws(() => assertLiveGateCoverage(input.series, truncated, periods),
    /coverage has a gap/);
  const missingEnd = gates.filter(gate => gate.pairNumber !== 1 ||
    gate.host !== 'soui' || Date.parse(gate.checkedAt) < start + 40000);
  assert.throws(() => assertLiveGateCoverage(input.series, missingEnd, periods),
    /coverage has a gap/);
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
});
