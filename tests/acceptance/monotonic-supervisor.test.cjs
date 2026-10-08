'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const {createSupervisor} = require('./monotonic-supervisor.cjs');

function fixture({timeoutMs = 20000, settleMs = 500} = {}) {
  const time = {monotonic: 125, wall: Date.UTC(2026, 9, 3)};
  const supervisor = createSupervisor({timeoutMs, settleMs}, {
    monotonicNow: () => time.monotonic,
    wallNow: () => time.wall,
  });
  return {time, supervisor};
}

test('forward wall-clock jumps do not expire a live native step', () => {
  const {time, supervisor} = fixture();
  supervisor.observe({event:'step.begin', elapsedMs:50, action:'mount', window:1, cycle:2});
  time.monotonic += 1000;
  time.wall += 60 * 60 * 1000;
  assert.equal(supervisor.deadlineReason(), null);
  const snapshot = supervisor.snapshot();
  assert.equal(snapshot.observedAt, new Date(time.wall).toISOString());
  assert.equal(snapshot.supervisorClock, 'monotonic');
  assert.equal(snapshot.monotonicElapsedMs, 1000);
  assert.equal(snapshot.lastProgressAgeMs, 1000);
  assert.deepEqual(snapshot.stepStarted, {startedElapsedMs:0, ageMs:1000, action:'mount', window:1, cycle:2});
  time.monotonic += 21000;
  assert.equal(supervisor.deadlineReason(), null, 'the existing 2 second step grace is preserved');
  time.monotonic += 1;
  assert.equal(supervisor.deadlineReason(), 'step-deadline:mount');
});

test('backward wall-clock jumps cannot postpone the native step deadline', () => {
  const {time, supervisor} = fixture();
  supervisor.observe({event:'step.begin', elapsedMs:0, action:'close', window:3, cycle:9});
  time.wall -= 24 * 60 * 60 * 1000;
  time.monotonic += 22001;
  assert.equal(supervisor.deadlineReason(), 'step-deadline:close');
  assert.equal(supervisor.snapshot().stepStarted.ageMs, 22001);
});

test('no-progress supervision uses monotonic elapsed time and the configured settle allowance', () => {
  const {time, supervisor} = fixture({settleMs:60000});
  time.wall += 24 * 60 * 60 * 1000;
  assert.equal(supervisor.deadlineReason(), null);
  time.wall -= 48 * 60 * 60 * 1000;
  time.monotonic += 65000;
  assert.equal(supervisor.deadlineReason(), null);
  time.monotonic += 1;
  assert.equal(supervisor.deadlineReason(), 'no-native-progress');
  assert.equal(supervisor.snapshot().lastProgressAgeMs, 65001);
});

test('a completed or failed step clears its deadline and new progress renews the idle deadline', () => {
  for (const event of ['step.complete', 'step.failed']) {
    const {time, supervisor} = fixture();
    supervisor.observe({event:'step.begin', action:'reload', window:1, cycle:1});
    time.monotonic += 21000;
    supervisor.observe({event, action:'reload', window:1, cycle:1});
    time.monotonic += 2000;
    assert.equal(supervisor.deadlineReason(), null);
    assert.equal(supervisor.snapshot().stepStarted, null);
    time.monotonic += 23001;
    assert.equal(supervisor.deadlineReason(), 'no-native-progress');
    supervisor.observe({event:'sample', elapsedMs:999999, phase:'cycle', cycle:1});
    assert.equal(supervisor.deadlineReason(), null);
    assert.equal(supervisor.snapshot().lastProgressAgeMs, 0);
  }
});

test('native timestamps and mutable diagnostic snapshots cannot change deadline state', () => {
  const {time, supervisor} = fixture();
  const record = {event:'step.begin', elapsedMs:999999999, action:'mount', window:1, cycle:1};
  supervisor.observe(record);
  record.action = 'forged';
  const snapshot = supervisor.snapshot();
  snapshot.stepStarted.action = 'mutated';
  snapshot.lastNativeRecord.action = 'mutated';
  time.monotonic += 22001;
  assert.equal(supervisor.deadlineReason(), 'step-deadline:mount');
  assert.equal(supervisor.snapshot().lastNativeRecord.action, 'mount');
  assert.equal(supervisor.snapshot().monotonicElapsedMs, 22001);
});
