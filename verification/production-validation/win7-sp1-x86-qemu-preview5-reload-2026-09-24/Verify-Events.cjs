'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {validateRecords} = require('../../../tests/acceptance/runner.cjs');

const root = path.resolve(process.argv[2] || path.join(__dirname, 'results'));
const raw = fs.readFileSync(path.join(root, 'reload-events.ndjson'), 'utf8');
if (!raw.endsWith('\n')) throw new Error('Truncated reload event stream');
const records = raw.trimEnd().split('\n').map(line => JSON.parse(line));
const config = {mode: 'reload', cycles: 100, warmup: 3, settleMs: 1000, timeoutMs: 60000};
const exitCode = Number(fs.readFileSync(path.join(root, 'reload-exitcode.txt'), 'utf8').trim());
const outcome = {pid: records[0]?.pid, exitCode, timedOut: false};
const validation = validateRecords(records, config, outcome);
if (validation.measuredCycles !== 100 || validation.resourceGate !== 'not-evaluated')
  throw new Error('Invalid reload validation result');
const cycles = validation.samples.filter(sample => sample.event === 'sample' && sample.phase === 'cycle');
const closed = validation.samples.find(sample => sample.phase === 'runtime-closed');
const summary = {schema: 'soui.win7-reload-summary/v1', cycles: validation.measuredCycles,
  completedSteps: validation.completedSteps, elapsedMs: records.at(-1).elapsedMs,
  warmupEnd: cycles[2], firstMeasured: cycles[3], lastMeasured: cycles.at(-1), runtimeClosed: closed,
  resourceGate: validation.resourceGate, releaseReady: false};
fs.writeFileSync(path.join(root, 'verified-summary.json'), JSON.stringify(summary, null, 2) + '\n');
process.stdout.write(JSON.stringify({cycles: summary.cycles, completedSteps: summary.completedSteps,
  elapsedMs: summary.elapsedMs, firstPrivateBytes: summary.firstMeasured.privateBytes,
  lastPrivateBytes: summary.lastMeasured.privateBytes}) + '\n');
