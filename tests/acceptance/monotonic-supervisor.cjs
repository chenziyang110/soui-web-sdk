'use strict';
const {performance} = require('node:perf_hooks');

function createSupervisor(config, {monotonicNow = () => performance.now(), wallNow = Date.now} = {}) {
  const started = monotonicNow();
  let lastProgress = started;
  let stepStarted = null;
  let lastNativeRecord = null;
  return {
    observe(record) {
      const now = monotonicNow();
      lastProgress = now;
      lastNativeRecord = Object.fromEntries(['event', 'elapsedMs', 'action', 'window', 'cycle']
        .filter(key => Object.hasOwn(record, key)).map(key => [key, record[key]]));
      if (record.event === 'step.begin')
        stepStarted = {at:now, action:record.action, window:record.window, cycle:record.cycle};
      if (record.event === 'step.complete' || record.event === 'step.failed') stepStarted = null;
    },
    deadlineReason() {
      const now = monotonicNow();
      if (stepStarted && now - stepStarted.at > config.timeoutMs + 2000)
        return 'step-deadline:' + stepStarted.action;
      if (!stepStarted && now - lastProgress > Math.max(config.timeoutMs, config.settleMs) + 5000)
        return 'no-native-progress';
      return null;
    },
    snapshot() {
      const now = monotonicNow();
      return {
        observedAt:new Date(wallNow()).toISOString(),
        supervisorClock:'monotonic',
        monotonicElapsedMs:now - started,
        lastProgressAgeMs:now - lastProgress,
        lastNativeRecord:lastNativeRecord ? {...lastNativeRecord} : null,
        stepStarted:stepStarted ? {startedElapsedMs:stepStarted.at - started, ageMs:now - stepStarted.at,
          action:stepStarted.action, window:stepStarted.window, cycle:stepStarted.cycle} : null,
      };
    },
  };
}

module.exports = {createSupervisor};
