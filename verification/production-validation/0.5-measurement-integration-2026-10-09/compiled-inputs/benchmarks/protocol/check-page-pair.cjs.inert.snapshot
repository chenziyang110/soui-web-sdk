'use strict';

// Page-result preflight only. A pass cannot establish physical presentation,
// process-tree resources, startup latency, or equal external input.
const fs = require('node:fs');
const assert = require('node:assert/strict');

const expectedFixture = {
  fixtureId: 'electron-smoothness-10k-v1',
  seed: 20260915,
  rowCount: 10000,
  pageSize: 200,
  rowHeightPx: 32,
};
const scenes = new Set(['S01', 'S02', 'S03', 'S04', 'S05']);

function finiteNonnegative(value) {
  return Number.isFinite(value) && value >= 0;
}

function sameFixture(value) {
  try { assert.deepStrictEqual(value, expectedFixture); return true; }
  catch { return false; }
}

function validateOne(value, host, errors) {
  if (!value || typeof value !== 'object' || Array.isArray(value)) {
    errors.push(`${host}:invalid-result`);
    return;
  }
  if (value.schemaVersion !== 1 || value.hostKind !== host || !scenes.has(value.scene))
    errors.push(`${host}:identity`);
  if (value.status !== 'measured' || value.sceneChecks?.ready !== true || value.sceneChecks?.completed !== true)
    errors.push(`${host}:not-completed`);
  if (!sameFixture(value.fixture)) errors.push(`${host}:fixture`);
  const viewport = value.viewport;
  if (!viewport || !Number.isInteger(viewport.width) || viewport.width <= 0 ||
      !Number.isInteger(viewport.height) || viewport.height <= 0 ||
      !Number.isFinite(viewport.devicePixelRatio) || viewport.devicePixelRatio <= 0)
    errors.push(`${host}:viewport`);
  if (!finiteNonnegative(value.timing?.startedAtMs) ||
      !finiteNonnegative(value.timing?.finishedAtMs) ||
      !finiteNonnegative(value.timing?.durationMs) ||
      Math.abs(value.timing.finishedAtMs - value.timing.startedAtMs - value.timing.durationMs) > 5 ||
      value.timing.durationMs < 60000)
    errors.push(`${host}:short-duration`);
  if (!Array.isArray(value.js?.eventLog) || !Array.isArray(value.js?.actionLog) ||
      !Array.isArray(value.js?.rafIntervalsMs) || !Array.isArray(value.js?.resizeEvents) ||
      !Array.isArray(value.js?.scrollEvents)) errors.push(`${host}:telemetry`);
  if (value.scene === 'S01') {
    const actions = value.js?.actionLog?.map(item => item.name);
    if (!actions?.includes('s01-switch') || !actions?.includes('s01-expand'))
      errors.push(`${host}:s01-actions`);
  } else if (value.scene === 'S02') {
    if (value.sceneChecks?.rowCount !== 10000 || value.sceneChecks?.uniqueRowCount !== 10000 ||
        !value.js?.scrollEvents?.length) errors.push(`${host}:s02-rows-scroll`);
  } else if (value.scene === 'S03') {
    if (!Number.isInteger(value.sceneChecks?.imeEvents) || value.sceneChecks.imeEvents < 2)
      errors.push(`${host}:s03-ime`);
  } else if (value.scene === 'S04') {
    if (!value.js?.resizeEvents?.length) errors.push(`${host}:s04-resize`);
  } else if (value.scene === 'S05') {
    if (value.sceneChecks?.motionRequiredMs !== 120000 ||
        !Number.isFinite(value.sceneChecks?.motionObservedMs) ||
        value.sceneChecks.motionObservedMs < 120000 ||
        !value.js?.rafIntervalsMs?.length) errors.push(`${host}:s05-motion`);
  }
}

function checkPagePair(soui, electron) {
  const errors = [];
  validateOne(soui, 'soui-miniblink', errors);
  validateOne(electron, 'electron', errors);
  if (soui?.scene !== electron?.scene) errors.push('scene-mismatch');
  if (soui?.viewport && electron?.viewport &&
      (soui.viewport.width !== electron.viewport.width ||
       soui.viewport.height !== electron.viewport.height ||
       soui.viewport.devicePixelRatio !== electron.viewport.devicePixelRatio))
    errors.push('viewport-mismatch');
  if (Array.isArray(soui?.js?.actionLog) && Array.isArray(electron?.js?.actionLog) &&
      JSON.stringify(soui.js.actionLog.map(item => item.name)) !==
      JSON.stringify(electron.js.actionLog.map(item => item.name)))
    errors.push('action-sequence-mismatch');
  return {
    schema: 'soui.benchmark-page-pair-preflight/v1',
    scene: soui?.scene ?? null,
    pagePairAccepted: errors.length === 0,
    errors,
    remainingEvidence: [
      'same-bundle-file-hashes', 'host-build-and-security-identity',
      'external-input-trace', 'physical-presentation',
      'process-tree-resources', 'startup-samples', 'repeat-count-and-run-order',
    ],
  };
}

function readResult(filename) {
  const size = fs.statSync(filename).size;
  if (size > 8 * 1024 * 1024) throw new Error('Result exceeds 8 MiB');
  return JSON.parse(fs.readFileSync(filename, 'utf8'));
}

if (require.main === module) {
  try {
    if (process.argv.length !== 4) throw new Error('Usage: node check-page-pair.cjs <soui-result.json> <electron-result.json>');
    const report = checkPagePair(readResult(process.argv[2]), readResult(process.argv[3]));
    process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
    if (!report.pagePairAccepted) process.exitCode = 2;
  } catch (error) {
    process.stderr.write(`${error.message}\n`);
    process.exitCode = 1;
  }
}

module.exports = {checkPagePair};
