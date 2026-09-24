'use strict';

const crypto = require('node:crypto');
const fs = require('node:fs');
const path = require('node:path');
const {validateMixedRecords} = require('./mixed-validation.cjs');

const resourceNames = ['privateBytes', 'workingSetBytes', 'handles', 'threads', 'gdiObjects', 'userObjects'];

function summarize(values) {
  if (!values.length) return null;
  const sorted = [...values].sort((left, right) => left - right);
  const percentile = fraction => sorted[Math.ceil(fraction * sorted.length) - 1];
  return {count:values.length, first:values[0], last:values.at(-1),
    min:sorted[0], max:sorted.at(-1), p50:percentile(0.5), p95:percentile(0.95)};
}

function analyzeMixed(result, records) {
  if (result?.passed !== true || result.releaseReady !== false ||
      !result.runId || !result.sourceSha256 || !result.payloadSha256)
    throw new Error('Mixed analysis requires a successful, identity-bound preview result');
  const verified = validateMixedRecords(records, result.config, result.outcome);
  for (const name of ['completedSteps', 'measuredCycles', 'elapsedMs'])
    if (result.validation?.[name] !== verified[name])
      throw new Error(`Mixed result disagrees with native events: ${name}`);

  const hours = new Map();
  const bucket = elapsedMs => {
    const hour = Math.floor(elapsedMs / 3600000);
    if (!hours.has(hour)) hours.set(hour, {hour, samples:[], steps:[]});
    return hours.get(hour);
  };
  let begun = null;
  for (const record of records) {
    if (record.event === 'sample' && record.phase === 'cycle') bucket(record.elapsedMs).samples.push(record);
    if (record.event === 'step.begin') begun = record;
    if (record.event === 'step.complete') {
      if (!begun || begun.action !== record.action || begun.window !== record.window ||
          begun.cycle !== record.cycle) throw new Error('Mixed step pairing differs');
      bucket(record.elapsedMs).steps.push({action:record.action,
        durationMs:record.elapsedMs - begun.elapsedMs});
      begun = null;
    }
  }
  if (begun) throw new Error('Mixed step did not complete');
  const hourly = [...hours.values()].sort((left, right) => left.hour - right.hour).map(group => {
    const resources = Object.fromEntries(resourceNames.map(name =>
      [name, summarize(group.samples.map(sample => sample[name]))]));
    const actions = Object.fromEntries([...new Set(group.steps.map(step => step.action))].sort()
      .map(action => [action, summarize(group.steps.filter(step => step.action === action)
        .map(step => step.durationMs))]));
    return {hour:group.hour, fromMs:group.hour * 3600000,
      toMs:(group.hour + 1) * 3600000, sampleCount:group.samples.length,
      stepCount:group.steps.length, firstCycle:group.samples[0]?.cycle ?? null,
      lastCycle:group.samples.at(-1)?.cycle ?? null, resources, actions};
  });
  return {schema:'soui.mixed-analysis/v1', runId:result.runId,
    sourceSha256:result.sourceSha256, payloadSha256:result.payloadSha256,
    requestedDurationMs:result.config.durationMs, elapsedMs:verified.elapsedMs,
    measuredCycles:verified.measuredCycles, completedSteps:verified.completedSteps,
    hourly, resourceGate:'not-evaluated', longRunGate:'not-evaluated', releaseReady:false};
}

function analyzeFile(resultPath) {
  const result = JSON.parse(fs.readFileSync(resultPath, 'utf8'));
  const raw = fs.readFileSync(path.join(path.dirname(resultPath), 'events.ndjson'));
  if (crypto.createHash('sha256').update(raw).digest('hex') !== result.logSha256 ||
      !raw.toString('utf8').endsWith('\n'))
    throw new Error('Mixed native event file does not match the result');
  const records = raw.toString('utf8').trimEnd().split('\n').map(line => JSON.parse(line));
  return analyzeMixed(result, records);
}

module.exports = {summarize, analyzeMixed, analyzeFile};
if (require.main === module) {
  try {
    if (process.argv.length !== 4) throw new Error('Usage: mixed-analysis.cjs RESULT_JSON OUTPUT_JSON');
    const output = analyzeFile(path.resolve(process.argv[2]));
    fs.writeFileSync(path.resolve(process.argv[3]), JSON.stringify(output, null, 2) + '\n', {flag:'wx'});
    console.log(JSON.stringify({runId:output.runId, hours:output.hourly.length,
      measuredCycles:output.measuredCycles, releaseReady:false}));
  } catch (error) { console.error(error.message); process.exitCode = 1; }
}
