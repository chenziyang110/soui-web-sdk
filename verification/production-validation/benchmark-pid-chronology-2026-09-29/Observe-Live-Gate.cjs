'use strict';

const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');

const expectedPid = Number(process.argv[2]);
assert(Number.isSafeInteger(expectedPid) && expectedPid > 0);
const root = __dirname;
const output = path.join(root, 'live-gate-observation-attempt1.json');
assert.equal(fs.existsSync(output), false, 'Refuse to replace observation');
const benchmark = require('C:/SOUI-SDK-development/benchmark-realtime-gate/benchmarks/protocol/run-series.cjs');
const scan = benchmark.liveInterferenceCheck(process.pid, new Map());
const ownCreatedAt = scan.owned.get(process.pid);
assert.equal(scan.rootPresent, true);
assert(typeof ownCreatedAt === 'string' && ownCreatedAt.length > 0);
const qemu = scan.competingProcesses.find(item => item.pid === expectedPid &&
  item.name.toLowerCase() === 'qemu-system-x86_64.exe');
assert(qemu, 'Expected unrelated QEMU was not detected');
const result = {
  schema: 'soui.benchmark-live-gate-observation/v1',
  checkedAt: scan.checkedAt, runnerPid: process.pid,
  runnerCreatedAt: ownCreatedAt,
  expectedExternalPid: expectedPid,
  detectedProcess: qemu,
  competingCount: scan.competingProcesses.length,
  rootPresent: scan.rootPresent,
  hostIsolationClaim: false, benchmarkValid: false, releaseReady: false,
};
fs.writeFileSync(output, JSON.stringify(result, null, 2) + '\n', {flag: 'wx'});
console.log(JSON.stringify({detectedExternalPid: expectedPid,
  competingCount: result.competingCount, benchmarkValid: false}));
