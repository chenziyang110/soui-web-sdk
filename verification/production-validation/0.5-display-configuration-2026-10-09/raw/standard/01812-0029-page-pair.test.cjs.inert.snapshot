'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const {checkPagePair} = require('../../benchmarks/protocol/check-page-pair.cjs');

const evidence = path.resolve(__dirname, '../../verification/production-validation/benchmark-host-smoke-2026-09-23');
function read(folder, prefix) {
  const name = fs.readdirSync(path.join(evidence, folder)).find(item => item.startsWith(prefix) && item.endsWith('.json'));
  return JSON.parse(fs.readFileSync(path.join(evidence, folder, name), 'utf8'));
}

test('historical smoke evidence is rejected for unequal content areas and short runs', () => {
  const soui = read('soui-x64-run5', 'soui-');
  const electron = read('electron-run1', '6a947807-9291-42d5-8657-e714cdb97dba-S01');
  const result = checkPagePair(soui, electron);
  assert.equal(result.pagePairAccepted, false);
  assert.ok(result.errors.includes('viewport-mismatch'));
  assert.ok(result.errors.includes('soui-miniblink:short-duration'));
  assert.ok(result.errors.includes('electron:short-duration'));
});

test('page preflight accepts only complete, aligned S01 telemetry and still lists external evidence', () => {
  const soui = read('soui-x64-run5', 'soui-');
  const electron = read('electron-run1', '6a947807-9291-42d5-8657-e714cdb97dba-S01');
  for (const result of [soui, electron]) {
    result.viewport = {width: 1100, height: 720, devicePixelRatio: 1};
    result.timing.finishedAtMs = result.timing.startedAtMs + 60000;
    result.timing.durationMs = 60000;
    result.js.actionLog = [{name: 's01-switch'}, {name: 's01-expand'}];
  }
  assert.equal(checkPagePair(soui, electron).pagePairAccepted, true);
  electron.js.actionLog[1].name = 's01-switch';
  assert.ok(checkPagePair(soui, electron).errors.includes('action-sequence-mismatch'));
  electron.js.actionLog[1].name = 's01-expand';
  electron.fixture.seed++;
  assert.ok(checkPagePair(soui, electron).errors.includes('electron:fixture'));
  electron.fixture.seed--;
  electron.sceneChecks.completed = false;
  assert.ok(checkPagePair(soui, electron).errors.includes('electron:not-completed'));
  assert.ok(checkPagePair(soui, electron).remainingEvidence.includes('physical-presentation'));
});

test('S05 requires observed motion for 120 seconds even when the page says measured', () => {
  const soui = read('soui-x64-run5', 'soui-');
  const electron = read('electron-run1', '6a947807-9291-42d5-8657-e714cdb97dba-S01');
  for (const result of [soui, electron]) {
    result.scene = 'S05';
    result.viewport = {width: 1100, height: 720, devicePixelRatio: 1};
    result.timing.durationMs = 120000;
    result.timing.finishedAtMs = result.timing.startedAtMs + 120000;
    result.sceneChecks.motionRequiredMs = 120000;
    result.sceneChecks.motionObservedMs = 119999;
    result.js.rafIntervalsMs = [16.7];
  }
  assert.ok(checkPagePair(soui, electron).errors.includes('soui-miniblink:s05-motion'));
  soui.sceneChecks.motionObservedMs = 120000;
  electron.sceneChecks.motionObservedMs = 120000;
  assert.equal(checkPagePair(soui, electron).pagePairAccepted, true);
});
