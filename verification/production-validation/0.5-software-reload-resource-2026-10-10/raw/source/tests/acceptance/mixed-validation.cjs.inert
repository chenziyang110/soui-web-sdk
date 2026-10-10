'use strict';

function mixedSettings(durationMs, settleMs, timeoutMs, idleMs) {
  const values = {durationMs, settleMs, timeoutMs, idleMs};
  for (const [name, maximum] of Object.entries({durationMs:43200000, settleMs:60000,
    timeoutMs:120000, idleMs:60000})) {
    if (!Number.isSafeInteger(values[name]) || values[name] < 0 || values[name] > maximum)
      throw new Error(`Invalid ${name}`);
  }
  if (durationMs < 1000 || timeoutMs < 1 || idleMs < 1 || idleMs >= timeoutMs)
    throw new Error('Invalid mixed duration, timeout or idle interval');
  return {mode:'mixed', durationMs, idleMs, settleMs, timeoutMs};
}

function expectedTranscript(cycles) {
  const records = [{event:'run.begin'}, {event:'sample', phase:'process', cycle:0}];
  const pair = (action, window, cycle) => {
    records.push({event:'step.begin', action, window, cycle});
    records.push({event:'step.complete', action, window, cycle});
  };
  for (let cycle = 1; cycle <= cycles; ++cycle) {
    records.push({event:'cycle.begin', cycle});
    pair('mount',1,cycle);
    if (cycle === 1) pair('runtime.reject-busy',0,cycle);
    pair('reload',1,cycle);
    pair('minimize',1,cycle);
    pair('restore',1,cycle);
    pair('mount',2,cycle);
    pair('mount',3,cycle);
    pair('close',2,cycle);
    pair('reload',1,cycle);
    pair('reload',3,cycle);
    pair('mount',2,cycle);
    pair('close',3,cycle);
    pair('close',2,cycle);
    pair('close',1,cycle);
    pair('idle',0,cycle);
    records.push({event:'sample',phase:'cycle',cycle});
    records.push({event:'cycle.complete',cycle});
  }
  const finalCycle = cycles + 1;
  records.push({event:'sample',phase:'closed',cycle:finalCycle});
  pair('runtime.shutdown',0,finalCycle);
  records.push({event:'sample',phase:'runtime-closed',cycle:finalCycle});
  pair('runtime.reject-restart',0,finalCycle);
  pair('runtime.shutdown-idempotent',0,finalCycle);
  records.push({event:'run.complete'});
  return records;
}

function validateMixedRecords(records, config, outcome) {
  mixedSettings(config.durationMs, config.settleMs, config.timeoutMs, config.idleMs);
  if (config.mode !== 'mixed' || outcome.timedOut !== false || outcome.exitCode !== 0 ||
      outcome.signal || !Number.isInteger(outcome.pid) || outcome.pid <= 0)
    throw new Error('Mixed process failed, timed out, or has no valid identity');
  if (!Array.isArray(records) || records.length < 4 || records[0].event !== 'run.begin' ||
      records.at(-1).event !== 'run.complete') throw new Error('Missing mixed run boundaries');
  if (records[0].pid !== outcome.pid || Object.keys(config).some(key => records[0][key] !== config[key]))
    throw new Error('Mixed scenario parameters or process identity differ');
  let elapsed = -1;
  const normalized = [];
  const samples = [];
  const idleStarts = new Map();
  for (const record of records) {
    if (!Number.isSafeInteger(record.elapsedMs) || record.elapsedMs < elapsed)
      throw new Error('Invalid mixed event timestamp');
    elapsed = record.elapsedMs;
    if (record.event === 'sample') {
      for (const key of ['privateBytes','workingSetBytes','handles','threads','gdiObjects','userObjects'])
        if (!Number.isSafeInteger(record[key]) || record[key] < 0)
          throw new Error(`Missing mixed resource measurement: ${key}`);
      samples.push(record);
      normalized.push({event:record.event,phase:record.phase,cycle:record.cycle});
    } else if (record.event === 'step.begin' || record.event === 'step.complete') {
      if (record.action === 'idle') {
        if (record.event === 'step.begin') idleStarts.set(record.cycle, record.elapsedMs);
        else if (!idleStarts.has(record.cycle) || record.elapsedMs - idleStarts.get(record.cycle) < config.idleMs - 2)
          throw new Error('Mixed idle interval was shortened');
      }
      normalized.push({event:record.event,action:record.action,window:record.window,cycle:record.cycle});
    } else if (record.event === 'cycle.begin' || record.event === 'cycle.complete') {
      normalized.push({event:record.event,cycle:record.cycle});
    } else normalized.push({event:record.event});
  }
  const cycles = records.filter(record => record.event === 'cycle.complete').length;
  if (cycles < 1 || records.at(-1).elapsedMs < config.durationMs)
    throw new Error('Mixed run ended before its requested duration');
  if (JSON.stringify(normalized) !== JSON.stringify(expectedTranscript(cycles)))
    throw new Error('Missing, duplicated, or reordered mixed lifecycle steps');
  return {completedSteps:records.filter(record => record.event === 'step.complete').length,
    measuredCycles:cycles, elapsedMs:records.at(-1).elapsedMs, samples,
    resourceGate:'not-evaluated', longRunGate:'not-evaluated', releaseReady:false};
}

module.exports = {mixedSettings, validateMixedRecords};
