'use strict';

const {test} = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const os = require('node:os');
const path = require('node:path');
const {compare, loadRun} = require('../../benchmarks/protocol/check-pair-runs.cjs');

const evidence = path.resolve(__dirname,
  '../../verification/production-validation/benchmark-s02-full-preflight-2026-09-25/raw');
const prior = path.resolve(__dirname,
  '../../verification/production-validation/benchmark-s01-preflight-2026-09-25/raw');

function pair(root, scene) {
  const soui = loadRun(path.join(root, `benchmark-${scene}-soui-20260925-run${scene === 's01' ? 2 : 1}`), 'soui');
  const electron = loadRun(path.join(root,
    `benchmark-${scene}-electron-20260925-run${scene === 's01' ? 3 : 1}`), 'electron');
  return compare(soui, electron);
}

test('full S02 raw traces and frozen inputs satisfy pairing preflight only', () => {
  const result = pair(evidence, 's02');
  assert.equal(result.preflightAccepted, true);
  assert.equal(result.benchmarkValid, false);
  assert.equal(result.releaseReady, false);
  assert.equal(result.checks.samePageBundle, true);
  assert.equal(result.checks.sameMachine, true);
});

test('historical completed S01 runs without frozen inputs remain rejected', () => {
  const result = pair(prior, 's01');
  assert.equal(result.preflightAccepted, false);
  assert.equal(result.checks.samePageBundle, false);
  assert.equal(result.checks.hostIdentity, false);
});

test('changed non-entry page asset invalidates a completed run', () => {
  const temp = fs.mkdtempSync(path.join(os.tmpdir(), 'soui-pair-preflight-'));
  assert.equal(path.dirname(temp), fs.realpathSync(os.tmpdir()));
  try {
    const source = path.join(evidence, 'benchmark-s02-soui-20260925-run1');
    fs.cpSync(source, path.join(temp, 'run'), {recursive: true});
    const file = path.join(temp, 'run', 'page-bundle', 'assets', 'benchmark-BFEd3JRA.css');
    fs.appendFileSync(file, '\n/* changed */');
    assert.throws(() => loadRun(path.join(temp, 'run'), 'soui'), /page bundle changed/);
  } finally {
    fs.rmSync(temp, {recursive: true, force: true});
  }
});
