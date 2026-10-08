'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {captureTimeout} = require('../../../../tests/acceptance/timeout-diagnostics.cjs');

const [runDir, dumpRoot, debuggerExe] = process.argv.slice(2);
if (process.argv.length !== 5 || !runDir || !dumpRoot || !debuggerExe ||
    ![runDir, dumpRoot, debuggerExe].every(path.isAbsolute))
  throw new Error('Usage: node watch-original.cjs ABSOLUTE_RUN_DIR ABSOLUTE_DUMP_ROOT ABSOLUTE_X86_CDB');
const events = path.join(runDir, 'events.ndjson');
const result = path.join(runDir, 'result.json');
const report = path.join(runDir, 'external-watch.json');
const runId = path.basename(runDir).slice('bridge-'.length);
if (!/^[0-9a-f-]{36}$/.test(runId)) throw new Error('Expected a Bridge run directory with a UUID');
let observedBytes = 0;
let lastGrowthAt = Date.now();
let lastNativeRecord = null;
let stepStarted = null;
let pid = null;
let finished = false;
let active = false;
const startedAt = Date.now();

function completeRecords(file) {
  const body = fs.readFileSync(file, 'utf8');
  const end = body.lastIndexOf('\n');
  return end < 0 ? [] : body.slice(0, end).split('\n').filter(Boolean).map(JSON.parse);
}
function finish(value, exitCode = 0) {
  if (finished) return;
  finished = true;
  clearInterval(timer);
  fs.writeFileSync(report, JSON.stringify(value, null, 2) + '\n', {flag: 'wx'});
  process.exitCode = exitCode;
}
async function tick() {
  if (finished || active) return;
  active = true;
  try {
    if (fs.existsSync(result)) return finish({status: 'no-stall-observed', runId, pid, observedBytes, lastNativeRecord});
    if (Date.now() - startedAt > 2 * 60 * 60 * 1000)
      return finish({status: 'watch-timeout', runId, pid, observedBytes, lastNativeRecord}, 2);
    if (!fs.existsSync(events)) return;
    const size = fs.statSync(events).size;
    if (size !== observedBytes) {
      observedBytes = size;
      lastGrowthAt = Date.now();
      const records = completeRecords(events);
      if (records[0]?.event === 'run.begin') pid = records[0].pid;
      stepStarted = null;
      for (const record of records) {
        if (record.event === 'step.begin') stepStarted = {action: record.action, cycle: record.cycle, window: record.window};
        if (record.event === 'step.complete' || record.event === 'step.failed') stepStarted = null;
      }
      lastNativeRecord = records.at(-1) || null;
    }
    if (!pid || lastNativeRecord?.event === 'run.complete' || Date.now() - lastGrowthAt < 8000) return;
    const snapshot = {schema: 'soui.acceptance-external-watch/v1', runId, reason: 'external-no-progress', pid,
      observedAt: new Date().toISOString(), observedBytes, lastProgressAgeMs: Date.now() - lastGrowthAt,
      lastNativeRecord, stepStarted};
    const diagnostic = await captureTimeout({pid, output: runDir, nativeLog: events, snapshot, debuggerExe, dumpRoot});
    finish({status: 'stall-observed', ...snapshot, diagnostic}, diagnostic.debugger.status === 'captured' ? 0 : 2);
  } catch (error) {
    finish({status: 'watch-error', runId, pid, observedBytes, error: error.message}, 2);
  } finally { active = false; }
}
const timer = setInterval(() => { void tick(); }, 500);
void tick();
