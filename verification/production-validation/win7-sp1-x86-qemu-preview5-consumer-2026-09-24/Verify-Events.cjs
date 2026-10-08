'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {validateRecords} = require('../../../tests/acceptance/runner.cjs');

const root = path.resolve(process.argv[2] || path.join(__dirname, 'results'));
for (const mode of ['bridge', 'reload', 'recreate', 'multiwindow']) {
  const raw = fs.readFileSync(path.join(root, `${mode}-events.ndjson`), 'utf8');
  if (!raw.endsWith('\n')) throw new Error(`${mode}: truncated event stream`);
  const records = raw.trimEnd().split('\n').map(line => JSON.parse(line));
  const config = {mode, cycles: 3, warmup: 0, settleMs: 100, timeoutMs: 60000};
  const outcome = {pid: records[0]?.pid, exitCode: 0, timedOut: false};
  const validation = validateRecords(records, config, outcome);
  if (validation.measuredCycles !== 3 || validation.resourceGate !== 'not-evaluated')
    throw new Error(`${mode}: invalid validation result`);
  process.stdout.write(`${mode}: ${validation.completedSteps} steps, ${validation.measuredCycles} cycles\n`);
}
