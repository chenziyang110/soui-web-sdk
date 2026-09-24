'use strict';

// Checks one host's external input and process-tree traces. This is an evidence
// integrity check, not a physical-presentation or performance verdict.
const fs = require('node:fs');

function fail(message) { throw new Error(message); }
function integer(value, minimum = 0) {
  return Number.isSafeInteger(value) && value >= minimum;
}
// Windows FILETIME is larger than the exact integer range of a JS number.
// Both traces encode it as JSON number; comparison is at JSON's precision.
function filetime(value) { return Number.isInteger(value) && value > 0; }
function readLines(filename) {
  const raw = fs.readFileSync(filename);
  if (!raw.length || raw.length > 32 * 1024 * 1024 || raw.at(-1) !== 10)
    fail('Trace must be nonempty, newline-terminated, and at most 32 MiB');
  return raw.toString('utf8').trimEnd().split('\n').map(line => JSON.parse(line));
}
function validateInput(records, scene) {
  const first = records[0];
  const last = records.at(-1);
  if (first?.event !== 'run.begin' || first.schema !== 'soui.external-input-trace/v1' ||
      first.scene !== scene || first.mode !== 'full' || first.contentWidth !== 1100 ||
      first.contentHeight !== 720 || !integer(first.pid, 1) ||
      !filetime(first.processCreationTime100ns) ||
      !filetime(first.startedAtUtcFiletime100ns) || !integer(first.qpcFrequency, 1))
    fail('Input trace has invalid full-run identity');
  if (last?.event !== 'run.end' || !integer(last.elapsedMs) ||
      last.elapsedMs < (scene === 'S05' ? 123000 : 61000) ||
      !integer(last.actions, 1) || last.actions !== records.length - 2)
    fail('Input trace has incomplete duration or action count');
  let elapsed = -1;
  let previousQpc = 0;
  for (const record of records.slice(1, -1)) {
    if (record.event !== 'input' || typeof record.action !== 'string' ||
        !integer(record.elapsedMs) || record.elapsedMs < elapsed ||
        !integer(record.qpcBefore, 1) || !integer(record.qpcAfter, 1) ||
        record.qpcBefore < previousQpc || record.qpcAfter < record.qpcBefore)
      fail('Input trace contains an invalid or reordered action');
    elapsed = record.elapsedMs;
    previousQpc = record.qpcAfter;
  }
  if (elapsed > last.elapsedMs ||
      !records.slice(1, -1).some(record => record.action === 'finish-scene'))
    fail('Input trace did not finish the scene');
  // The current driver emits Unicode key events, never an IME composition.
  // A writable flag in this trace cannot prove a real IME path.
  if (scene === 'S03') fail('S03 requires real IME composition evidence');
  return {pid:first.pid, creation:first.processCreationTime100ns,
    started:first.startedAtUtcFiletime100ns, durationMs:last.elapsedMs,
    actions:last.actions};
}
function validateProcessTree(records) {
  const first = records[0];
  const last = records.at(-1);
  if (first?.event !== 'run.begin' || first.schema !== 'soui.process-tree-samples/v1' ||
      !integer(first.rootPid, 1) || !filetime(first.rootCreationTime100ns) ||
      !filetime(first.startedAtUtcFiletime100ns) ||
      !integer(first.intervalMs, 1) || !integer(first.durationMs, 1))
    fail('Process-tree trace has invalid identity');
  const samples = records.slice(1, -1);
  if (last?.event !== 'run.end' || last.durationExpired !== false ||
      last.rootExitCode !== 0 || last.incompleteSamples !== 0 ||
      last.samples !== samples.length || !samples.length ||
      !integer(last.elapsedMs))
    fail('Process-tree trace is incomplete or host did not exit cleanly');
  let elapsed = -1;
  const maximumGap = first.intervalMs * 3 + 500;
  for (const sample of samples) {
    if (sample.event !== 'sample' || sample.complete !== true ||
        sample.rootPid !== first.rootPid || !integer(sample.elapsedMs) ||
        sample.elapsedMs < elapsed ||
        sample.elapsedMs - Math.max(elapsed, 0) > maximumGap ||
        !integer(sample.privateBytes) ||
        !integer(sample.workingSetBytes) || !integer(sample.handles) ||
        !integer(sample.threads) || !Array.isArray(sample.processes) ||
        !sample.processes.some(process => process.pid === first.rootPid &&
          process.creationTime100ns === first.rootCreationTime100ns) ||
        (sample.cpuPercent !== null &&
          (!Number.isFinite(sample.cpuPercent) || sample.cpuPercent < 0)))
      fail('Process-tree trace contains an invalid or incomplete sample');
    elapsed = sample.elapsedMs;
  }
  if (elapsed > last.elapsedMs || last.elapsedMs - elapsed > maximumGap)
    fail('Process-tree sampling does not cover the full host lifetime');
  return {pid:first.rootPid, creation:first.rootCreationTime100ns,
    started:first.startedAtUtcFiletime100ns, durationMs:last.elapsedMs,
    samples:samples.length};
}
function checkRunTraces(inputRecords, processRecords, scene) {
  if (!['S01', 'S02', 'S03', 'S04', 'S05'].includes(scene)) fail('Unknown scene');
  const input = validateInput(inputRecords, scene);
  const process = validateProcessTree(processRecords);
  if (input.pid !== process.pid || input.creation !== process.creation)
    fail('Input and process-tree traces identify different host processes');
  const offsetMs = (input.started - process.started) / 10000;
  if (offsetMs < 0 || offsetMs + input.durationMs > process.durationMs + 1000)
    fail('Input trace lies outside process-tree collection interval');
  return {schema:'soui.benchmark-run-traces/v1', scene,
    hostPid:input.pid, hostCreationTime100ns:input.creation,
    inputActions:input.actions, processSamples:process.samples,
    inputDurationMs:input.durationMs, processDurationMs:process.durationMs,
    inputOffsetMs:offsetMs, traceIntegrityAccepted:true,
    physicalPresentation:'not-measured', performanceGate:'not-evaluated',
    releaseReady:false};
}

if (require.main === module) {
  try {
    if (process.argv.length !== 5)
      fail('Usage: node check-run-traces.cjs SCENE INPUT_TRACE.ndjson PROCESS_TREE.ndjson');
    console.log(JSON.stringify(checkRunTraces(readLines(process.argv[3]),
      readLines(process.argv[4]), process.argv[2]), null, 2));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}

module.exports = {checkRunTraces, readLines};
