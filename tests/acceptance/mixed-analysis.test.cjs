'use strict';

const test = require('node:test');
const assert = require('node:assert/strict');
const path = require('node:path');
const {analyzeFile, analyzeMixed, summarize} = require('./mixed-analysis.cjs');

const evidence = path.resolve(__dirname,
  '../../verification/production-validation/preview5-mixed-soak-preflight-2026-09-24/x86-60s/result.json');

test('mixed analysis rechecks archived raw evidence and preserves the release gate', () => {
  const analysis = analyzeFile(evidence);
  assert.equal(analysis.measuredCycles, 4);
  assert.equal(analysis.completedSteps, 60);
  assert.equal(analysis.hourly.length, 1);
  assert.equal(analysis.hourly[0].sampleCount, 4);
  assert.equal(analysis.hourly[0].stepCount, 60);
  assert.equal(analysis.hourly[0].actions.idle.count, 4);
  assert.equal(analysis.hourly[0].actions.mount.count, 16);
  assert.equal(analysis.longRunGate, 'not-evaluated');
  assert.equal(analysis.releaseReady, false);
});

test('mixed analysis rejects contradictory result claims', () => {
  const result = require(evidence);
  assert.throws(() => analyzeMixed({...result, passed:false}, []), /successful/);
  assert.throws(() => analyzeMixed({...result, releaseReady:true}, []), /successful/);
});

test('hourly statistics retain first and last values and use nearest-rank percentiles', () => {
  assert.deepEqual(summarize([9, 1, 5, 7]),
    {count:4, first:9, last:7, min:1, max:9, p50:5, p95:9});
  assert.equal(summarize([]), null);
});
