'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {validateMixedRecords} = require('../../../tests/acceptance/mixed-validation.cjs');

const root = path.resolve(process.argv[2] || path.join(__dirname, 'results'));
const input = JSON.parse(fs.readFileSync(path.join(__dirname, 'input.json'), 'utf8'));
if (input.schema !== 'soui.win7-mixed-transfer/v1' || input.architecture !== 'x86' ||
    input.guestSystem !== 'Windows 7 Enterprise SP1 x86 QEMU' || input.releaseReady !== false)
  throw new Error('Unexpected Win7 mixed input identity');
const raw = fs.readFileSync(path.join(root, 'mixed-events.ndjson'), 'utf8');
if (!raw.endsWith('\n')) throw new Error('Truncated mixed event stream');
const records = raw.trimEnd().split('\n').map(line => JSON.parse(line));
const exitCode = Number(fs.readFileSync(path.join(root, 'mixed-exitcode.txt'), 'utf8').trim());
const outcome = {pid:records[0]?.pid, exitCode, signal:null, timedOut:false};
const validation = validateMixedRecords(records, input.guestConfig, outcome);
if (validation.measuredCycles < 1 || validation.longRunGate !== 'not-evaluated' ||
    validation.resourceGate !== 'not-evaluated' || validation.releaseReady !== false)
  throw new Error('Invalid Win7 mixed validation result');
const summary = {schema:'soui.win7-mixed-summary/v1', inputSourceSha256:input.sourceSha256,
  inputPayloadSha256:input.hostPayloadSha256, guestConfig:input.guestConfig,
  pid:outcome.pid, exitCode, measuredCycles:validation.measuredCycles,
  completedSteps:validation.completedSteps, elapsedMs:validation.elapsedMs,
  firstCycle:validation.samples.find(sample => sample.phase === 'cycle'),
  lastCycle:validation.samples.filter(sample => sample.phase === 'cycle').at(-1),
  runtimeClosed:validation.samples.find(sample => sample.phase === 'runtime-closed'),
  resourceGate:'not-evaluated', longRunGate:'not-evaluated', releaseReady:false};
fs.writeFileSync(path.join(root, 'verified-summary.json'), JSON.stringify(summary, null, 2) + '\n');
process.stdout.write(JSON.stringify({cycles:summary.measuredCycles,
  completedSteps:summary.completedSteps, elapsedMs:summary.elapsedMs}) + '\n');
