'use strict';
const fs = require('node:fs');
const path = require('node:path');
const {validateRecords} = require('../../../tests/acceptance/runner.cjs');

const root = path.resolve(process.argv[2] || path.join(__dirname, 'results'));
const input = JSON.parse(fs.readFileSync(path.join(__dirname, 'input.json'), 'utf8'));
const raw = fs.readFileSync(path.join(root, 'bridge-events.ndjson'), 'utf8');
if (!raw.endsWith('\n')) throw new Error('Truncated bridge event stream');
if (fs.readFileSync(path.join(root, 'bridge-exitcode.txt'), 'utf8').trim() !== '0')
  throw new Error('Bridge process did not exit successfully');
const records = raw.trimEnd().split('\n').map(line => JSON.parse(line));
const outcome = {pid: records[0]?.pid, exitCode: 0, timedOut: false};
const validation = validateRecords(records, input.config, outcome);
if (validation.completedSteps !== 10 || validation.measuredCycles !== 3 ||
    validation.resourceGate !== 'not-evaluated' || validation.releaseReady !== false)
  throw new Error('Unexpected public task acceptance result');
process.stdout.write(`bridge: ${validation.completedSteps} lifecycle steps, ${validation.measuredCycles} cycles, ${records.length} records\n`);
