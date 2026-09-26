'use strict';
// Synthetic parser tests only. Never starts a process, SDK, browser or GUI.
const fs = require('node:fs');
const path = require('node:path');
const assert = require('node:assert/strict');
const crypto = require('node:crypto');
const { verifyRun, strictJson, manifest, bindPayload } = require('./Verify-Run.cjs');
const sha = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
const suite = path.join(__dirname, 'self-test-only', new Date().toISOString().replace(/[:.]/g, '-') + '-' + process.pid);
fs.mkdirSync(suite, { recursive: true });
const output = [];
function baseline(durationMs = 30000, retry = false) {
  const rows = [], sessionId = 'synthetic-session', origin = 'app://installed-renewal-probe';
  const runId = 'synthetic-run', longTaskId = 'synthetic-long', resultTaskId = 'synthetic-result', subscriptionId = 'synthetic-sub';
  const fixed = { documentNonce: 'synthetic-document', sessionId, longTaskId, resultTaskId, subscriptionId };
  const counts = { bootstrapCount: 1, startCount: 2, subscribeCount: 1 };
  const identity = { sessionId, origin, sameAuthorization: true };
  let request = 0, generation = 1, eventCount = 0, eventSequence = 0, clock = 1, querySequence = 2;
  const emit = (elapsedMs, event, data = {}) => rows.push({ schema: 'soui.renewal-probe/v2', elapsedMs, event, ...data });
  const web = (at, data) => emit(at, 'web.record', { ...identity, dataJson: JSON.stringify(data) });
  const proto = (at, observations) => {
    for (let i = 0; i < observations.length; i += 12) web(at, { kind: 'protocol', observations: observations.slice(i, i + 12) });
  };
  function pair(method, params = {}, result = {}, extra = {}) {
    const requestId = 'synthetic-request-' + (++request);
    const first = { method, requestId, ...params, observedElapsedMs: clock++ };
    if (method !== 'bridge.bootstrap') Object.assign(first, { sessionId, origin });
    const second = { method, requestId, ok: true, ...result, observedElapsedMs: clock++, ...extra };
    return [first, second];
  }
  function lease() { return { generation, expiresInMs: 1800000 - (durationMs === 30000 ? 1000 : 0), renewAfterMs: 1499000 }; }
  emit(0, 'run.started', { durationMs, version: '0.3.0-preview.15', architecture: 'x86', runId, pid: 4242,
    realRenewalRequired: durationMs !== 30000, synthetic: true });
  emit(10, 'host.mounted', { mountCount: 1, reloadCount: 0 });
  emit(11, 'session.shutdown', { sessionId: 'opaque-synthetic-lifecycle', phase: 'before-bootstrap',
    classification: 'pre-bootstrap-lifecycle', bootstrapCount: 0, bound: false, readyCount: 0 });
  emit(12, 'bridge.bootstrap.begin', { bootstrapCount: 1, phase: 'bootstrap-in-flight' });
  emit(13, 'bridge.bootstrap.bound', { sessionId, origin, bootstrapCount: 1, phase: 'bound-before-describe' });
  emit(20, 'probe.describe', identity);
  emit(30, 'task.invoked', { kind: 'probe.long', count: 1, sessionId, operationId: runId + ':long' });
  emit(40, 'task.invoked', { kind: 'probe.result', count: 1, sessionId, operationId: runId + ':result' });
  emit(41, 'task.stopped', { kind: 'probe.result', reason: 'completed' });
  function progress(at) {
    ++eventCount; eventSequence += 2;
    emit(at, 'native.event', { dataJson: JSON.stringify({ sessionId, taskId: longTaskId, eventName: 'task.progress', sequence: eventSequence, payload: {} }) });
  }
  progress(50);
  proto(60, [
    ...pair('bridge.bootstrap', {}, { sessionId, origin, generation: 1 }),
    ...pair('probe.describe'),
    ...pair('task.start', { kind: 'probe.long', operationId: runId + ':long' }, { taskId: longTaskId, accepted: true, duplicate: false }),
    ...pair('task.start', { kind: 'probe.result', operationId: runId + ':result' }, { taskId: resultTaskId, accepted: true, duplicate: false }),
    ...pair('task.subscribe', { taskId: longTaskId }, { subscriptionId }),
    ...pair('task.query', { taskId: resultTaskId }, { taskId: resultTaskId, state: 'succeeded', sequence: 2, resultValue: 42 })
  ]);
  web(70, { kind: 'begin', ...fixed, initialGeneration: 1, durationMs });
  emit(80, 'probe.ready', { ...identity, count: 1 });
  const readiness = [...pair('bridge.probe.report'), ...pair('probe.ready')];
  for (let elapsedMs = 1000; elapsedMs <= durationMs; elapsedMs += 1000) {
    progress(80 + elapsedMs - 100);
    if (elapsedMs % 5000) continue;
    const at = 80 + elapsedMs;
    clock = at - 30;
    const observations = elapsedMs === 5000 ? readiness : [];
    if (elapsedMs % 1500000 === 0) {
      const operationId = 'synthetic-renew-' + (++generation);
      if (retry) {
        const lost = pair('bridge.session.renew', { operationId });
        delete lost[1].ok; Object.assign(lost[1], { operationId, transportError: true, errorCode: 'E_TRANSPORT' });
        observations.push(...lost);
        observations.push(...pair('bridge.session.renew', { operationId }, {}, { ok: false, errorCode: 'E_BUSY' }));
      }
      observations.push(...pair('bridge.session.renew', { operationId }, { generation, sessionId, origin }));
    }
    querySequence = eventSequence;
    observations.push(...pair('task.query', { taskId: longTaskId }, { taskId: longTaskId, state: 'running', sequence: querySequence }),
      ...pair('task.query', { taskId: resultTaskId }, { taskId: resultTaskId, state: 'succeeded', sequence: 2, resultValue: 42 }));
    proto(at - 1, observations);
    web(at, { kind: 'heartbeat', ...fixed, ...counts, elapsedMs, lease: lease(), eventCount, lastEventSequence: eventSequence,
      lastEventElapsedMs: elapsedMs - 100, querySequence,
      postOriginalExpiryEvent: elapsedMs > 1800000, postOriginalExpiryHeartbeat: elapsedMs > 1800000 });
    emit(at + 1, 'resource.sample', { privateBytes: 10000, workingSetBytes: 10000, handles: 10, threads: 4,
      gdiObjects: 1, userObjects: 2, resourceGate: 'not-evaluated' });
  }
  const at = 80 + durationMs;
  clock = at + 10;
  const cleanup = [
    ...pair('task.cancel', { taskId: longTaskId }, { taskId: longTaskId, cancelRequested: true, state: 'cancel-pending' }),
    ...pair('task.query', { taskId: longTaskId }, { taskId: longTaskId, state: 'cancelled', sequence: querySequence + 2 }),
    ...pair('task.unsubscribe', { subscriptionId }, { subscriptionId, removed: true }),
    ...pair('task.query', { taskId: resultTaskId }, { taskId: resultTaskId, state: 'succeeded', sequence: 2, resultValue: 42 })
  ];
  emit(at + 12, 'task.stopped', { kind: 'probe.long', reason: 'cancelled' });
  proto(at + 25, cleanup);
  web(at + 26, { kind: 'complete', ...fixed, ...counts, elapsedMs: durationMs + 26, lease: lease(),
    eventCount, lastEventSequence: eventSequence, querySequence, unsubscribeCount: 1, cancelled: true, unsubscribed: true,
    resultValue: 42, postOriginalExpiryEvent: durationMs > 1800000, postOriginalExpiryHeartbeat: durationMs > 1800000,
    realRenewalVerified: durationMs !== 30000 });
  emit(at + 30, 'probe.finish', identity);
  emit(at + 529, 'session.shutdown', { sessionId, phase: 'closing-bound', classification: 'bound-close',
    bootstrapCount: 1, bound: true, readyCount: 1 });
  emit(at + 530, 'host.destroyed', { ok: true, code: '', activeScope: false });
  emit(at + 531, 'runtime.shutdown', { ok: true, code: '' });
  emit(at + 535, 'run.finished', { exitCode: 0, readyCount: 1, longInvocations: 1, resultInvocations: 1,
    shutdownSessions: 2, bootstrapCount: 1, prebootstrapRetirements: 1, boundShutdowns: 1,
    realRenewalVerified: false, releaseReady: false });
  rows.sort((a, b) => a.elapsedMs - b.elapsedMs).forEach((row, i) => { row.seq = i + 1; });
  return { rows, supervisor: { exitCode: 0, timedOut: false, payloadUnchanged: true, synthetic: true,
    inputsUnchanged: true, supervisionSucceeded: true, externalLoadDetected: false, remainingDesktopProcesses: [],
    reportPath: path.join(suite, 'synthetic-memory-input.ndjson'), pid: 4242, architecture: 'x86', durationMs } };
}
function bytes(rows) { return Buffer.from(rows.map(row => JSON.stringify(row)).join('\n') + '\n'); }
function check(data) {
  const report = bytes(data.rows), supervisor = Buffer.from(JSON.stringify({ ...data.supervisor, reportSha256: sha(report) }));
  return verifyRun(report, supervisor, { synthetic: true });
}
function eachObservation(data, callback) {
  for (const row of data.rows.filter(r => r.event === 'web.record')) {
    const value = JSON.parse(row.dataJson);
    if (value.kind === 'protocol') {
      for (const observation of value.observations) callback(observation, value, row);
      row.dataJson = JSON.stringify(value);
    }
  }
}
function webChange(data, kind, change, all = false) {
  for (const row of data.rows.filter(r => r.event === 'web.record')) {
    const value = JSON.parse(row.dataJson);
    if (value.kind === kind) { change(value, row); row.dataJson = JSON.stringify(value); if (!all) return; }
  }
}
function test(name, body) {
  try { body(); output.push({ name, passed: true }); console.log('PASS ' + name); }
  catch (error) { output.push({ name, passed: false, error: error.message }); console.log('FAIL ' + name + ': ' + error.message); }
}
for (const [name, duration, retry] of [['synthetic-smoke', 30000, false], ['synthetic-35min', 2100000, false],
  ['synthetic-35min-transport-recovery', 2100000, true], ['synthetic-8h', 28800000, false]]) {
  test(name, () => {
    const input = baseline(duration, retry), result = check(input);
    assert.equal(result.evidenceValidated, true); assert.equal(result.syntheticOnly, true);
    for (const flag of ['harnessSmokeVerified', 'realRenewalVerified', 'scenarioEightHourVerified', 'releaseReady', 'benchmarkValid', 'resourceGate'])
      assert.equal(result[flag], false);
    fs.writeFileSync(path.join(suite, name + '.ndjson'), bytes(input.rows), { flag: 'wx' });
    fs.writeFileSync(path.join(suite, name + '.supervisor.json'), JSON.stringify({ ...input.supervisor,
      reportPath: path.join(suite, name + '.ndjson'), reportSha256: sha(bytes(input.rows)) }, null, 2) + '\n', { flag: 'wx' });
  });
}
const mutations = [
  ['second-native-bootstrap', d => { d.rows.push({...d.rows.find(r => r.event === 'bridge.bootstrap.begin')});
    d.rows.sort((a,b) => a.elapsedMs-b.elapsedMs).forEach((r,i) => r.seq=i+1); }],
  ['v1-run-cannot-enter-v2', d => { for (const row of d.rows) row.schema = 'soui.renewal-probe/v1'; }],
  ['missing-native-bootstrap', d => { d.rows.find(r => r.event === 'bridge.bootstrap.bound').event = 'resource.sample'; }],
  ['native-bootstrap-identity-changed', d => { d.rows.find(r => r.event === 'bridge.bootstrap.bound').sessionId = 'changed'; }],
  ['retirement-with-bound-identity', d => { d.rows.find(r => r.event === 'session.shutdown').sessionId = 'synthetic-session'; }],
  ['retirement-after-bootstrap-begins', d => { const row = d.rows.find(r => r.event === 'session.shutdown'); row.elapsedMs = 14;
    d.rows.sort((a,b) => a.elapsedMs-b.elapsedMs).forEach((r,i) => r.seq=i+1); }],
  ['retirement-after-bootstrap-binding', d => { const row = d.rows.find(r => r.event === 'session.shutdown'); row.phase = 'bound-before-describe'; row.bound = true; row.bootstrapCount = 1; }],
  ['second-prebootstrap-retirement', d => { const row = {...d.rows.find(r => r.event === 'session.shutdown')}; d.rows.push(row);
    const end = d.rows[d.rows.length-2]; end.prebootstrapRetirements = 2; end.shutdownSessions = 3;
    d.rows.sort((a,b) => a.elapsedMs-b.elapsedMs).forEach((r,i) => r.seq=i+1); }],
  ['unexpected-retirement-classification', d => { d.rows.find(r => r.event === 'session.shutdown').classification = 'rejected'; }],
  ['bound-shutdown-before-describe', d => { const row = d.rows.find(r => r.classification === 'bound-close'); row.elapsedMs = 15; row.phase = 'bound-before-describe'; row.readyCount = 0;
    d.rows.sort((a,b) => a.elapsedMs-b.elapsedMs).forEach((r,i) => r.seq=i+1); }],
  ['bound-shutdown-before-ready', d => { const row = d.rows.find(r => r.classification === 'bound-close'); row.elapsedMs = 75; row.phase = 'described-before-ready'; row.readyCount = 0;
    d.rows.sort((a,b) => a.elapsedMs-b.elapsedMs).forEach((r,i) => r.seq=i+1); }],
  ['bound-shutdown-after-ready-before-finish', d => { const row = d.rows.find(r => r.classification === 'bound-close'); row.elapsedMs = 30000; row.phase = 'ready';
    d.rows.sort((a,b) => a.elapsedMs-b.elapsedMs).forEach((r,i) => r.seq=i+1); }],
  ['bound-shutdown-wrong-id', d => { d.rows.find(r => r.classification === 'bound-close').sessionId = 'wrong'; }],
  ['bound-shutdown-missing', d => { d.rows.find(r => r.classification === 'bound-close').event = 'resource.sample'; }],
  ['bound-shutdown-duplicate', d => { d.rows.push({...d.rows.find(r => r.classification === 'bound-close')});
    d.rows.sort((a,b) => a.elapsedMs-b.elapsedMs).forEach((r,i) => r.seq=i+1); }],
  ['final-retirement-count-mismatch', d => { d.rows[d.rows.length-1].prebootstrapRetirements = 0; }],
  ['final-bound-close-count-mismatch', d => { d.rows[d.rows.length-1].boundShutdowns = 0; }],
  ['duplicate-native-sequence', d => { d.rows[1].seq = 1; }],
  ['missing-native-row', d => { d.rows.splice(2, 1); }],
  ['native-clock-regression', d => { d.rows[2].elapsedMs = 0; }],
  ['second-mount', d => { d.rows.find(r => r.event === 'host.mounted').mountCount = 2; }],
  ['failure-marker', d => { d.rows[3].event = 'run.failed'; }],
  ['wrong-version', d => { d.rows[0].version = '0.3.0-preview.10'; }],
  ['unexpected-duration', d => { d.rows[0].durationMs = 29999; }],
  ['supervisor-string-zero', d => { d.supervisor.exitCode = '0'; }],
  ['supervisor-nonzero', d => { d.supervisor.exitCode = 1; }],
  ['supervisor-timeout', d => { d.supervisor.timedOut = true; }],
  ['supervisor-payload-changed', d => { d.supervisor.payloadUnchanged = false; }],
  ['supervisor-input-changed', d => { d.supervisor.inputsUnchanged = false; }],
  ['supervisor-post-check-failed', d => { d.supervisor.supervisionSucceeded = false; }],
  ['supervisor-external-load-observed', d => { d.supervisor.externalLoadDetected = true; }],
  ['supervisor-desktop-process-remains', d => { d.supervisor.remainingDesktopProcesses = [{ ProcessId: 1 }]; }],
  ['supervisor-desktop-processes-missing', d => { delete d.supervisor.remainingDesktopProcesses; }],
  ['supervisor-wrong-pid', d => { d.supervisor.pid = 4243; }],
  ['supervisor-wrong-architecture', d => { d.supervisor.architecture = 'x64'; }],
  ['supervisor-wrong-duration', d => { d.supervisor.durationMs = 2100000; }],
  ['native-session-change', d => { d.rows.find(r => r.event === 'probe.ready').sessionId = 'another'; }],
  ['native-authorization-change', d => { d.rows.find(r => r.event === 'probe.ready').sameAuthorization = false; }],
  ['replayed-task-invocation', d => { d.rows.find(r => r.event === 'task.invoked').count = 2; }],
  ['task-operation-change', d => { d.rows.find(r => r.event === 'task.invoked').operationId = 'other'; }],
  ['task-ended-without-cancel', d => { d.rows.find(r => r.event === 'task.stopped' && r.kind === 'probe.long').reason = 'completed'; }],
  ['document-change', d => webChange(d, 'heartbeat', v => { v.documentNonce = 'another'; })],
  ['subscription-change', d => webChange(d, 'heartbeat', v => { v.subscriptionId = 'another'; })],
  ['web-task-recreated', d => webChange(d, 'heartbeat', v => { v.startCount = 3; })],
  ['web-failure-marker', d => webChange(d, 'heartbeat', v => { v.kind = 'failure'; })],
  ['heartbeat-sequence-no-advance', d => webChange(d, 'heartbeat', v => { v.lastEventSequence = 2; v.eventCount = 2; }, true)],
  ['heartbeat-no-native-match', d => webChange(d, 'heartbeat', v => { v.lastEventSequence = 999999; })],
  ['heartbeat-polling-only', d => webChange(d, 'heartbeat', v => { v.eventCount = 0; })],
  ['heartbeat-impossible-event-count', d => webChange(d, 'heartbeat', v => { v.eventCount = 9999; })],
  ['lease-expired', d => webChange(d, 'heartbeat', v => { v.lease.expiresInMs = 0; })],
  ['lease-generation-not-acked', d => webChange(d, 'heartbeat', v => { v.lease.generation = 2; })],
  ['lease-extended-limit', d => webChange(d, 'heartbeat', v => { v.lease.expiresInMs = 1800001; })],
  ['cancel-not-requested', d => eachObservation(d, o => { if (o.method === 'task.cancel' && o.ok) o.cancelRequested = false; })],
  ['cancel-not-confirmed', d => eachObservation(d, o => { if (o.state === 'cancelled') o.state = 'running'; })],
  ['unsubscribe-not-removed', d => eachObservation(d, o => { if (o.method === 'task.unsubscribe' && o.ok) o.removed = false; })],
  ['retained-result-lost', d => eachObservation(d, o => { if (o.resultValue === 42) o.resultValue = 41; })],
  ['response-request-mismatch', d => { let done = false; eachObservation(d, o => { if (!done && o.ok) { o.requestId = 'orphan'; done = true; } }); }],
  ['observation-clock-regression', d => eachObservation(d, o => { if (o.method === 'task.cancel') o.observedElapsedMs = 0; })],
  ['observer-unknown-raw-field', d => eachObservation(d, o => { if (o.method === 'task.cancel') o.token = 'should-not-appear'; })],
  ['business-error', d => eachObservation(d, o => { if (o.method === 'task.cancel' && o.ok) { o.ok = false; o.errorCode = 'E_BUSY'; } })],
  ['duplicate-start-ack', d => eachObservation(d, o => { if (o.method === 'task.start' && o.ok) o.duplicate = true; })],
  ['cleanup-scope-active', d => { d.rows.find(r => r.event === 'host.destroyed').activeScope = true; }],
  ['runtime-cleanup-failed', d => { d.rows.find(r => r.event === 'runtime.shutdown').ok = false; }],
  ['native-exit-nonzero', d => { d.rows.at(-1).exitCode = 1; }],
  ['smoke-overclaim', d => webChange(d, 'complete', v => { v.realRenewalVerified = true; })],
  ['native-truncated-lifecycle', d => { d.rows.pop(); }],
  ['resource-overclaim', d => { d.rows.find(r => r.event === 'resource.sample').resourceGate = 'passed'; }]
];
for (const [name, mutate] of mutations) test(name, () => { const d = baseline(); mutate(d); assert.throws(() => check(d)); });
test('synthetic-no-initial-lifecycle-retirement', () => {
  const d = baseline();
  d.rows = d.rows.filter(row => row.classification !== 'pre-bootstrap-lifecycle');
  d.rows.forEach((row, i) => { row.seq = i + 1; });
  Object.assign(d.rows[d.rows.length - 1], {prebootstrapRetirements: 0, shutdownSessions: 1});
  const result = check(d);
  assert.equal(result.prebootstrapRetirements, 0);
  assert.equal(result.boundShutdowns, 1);
  assert.equal(result.realRenewalVerified, false);
});
test('duplicate-json-member', () => assert.throws(() => strictJson('{"exitCode":1,"exitCode":0}', 'test')));
test('truncated-last-line', () => {
  const d = baseline(), report = bytes(d.rows);
  const truncated = report.subarray(0, report.length - 1);
  assert.throws(() => verifyRun(truncated, Buffer.from(JSON.stringify({ ...d.supervisor, reportSha256: sha(truncated) })), { synthetic: true }), /truncated/);
});
test('bad-report-hash', () => {
  const d = baseline();
  assert.throws(() => verifyRun(bytes(d.rows), Buffer.from(JSON.stringify({ ...d.supervisor, reportSha256: '0'.repeat(64) })), { synthetic: true }));
});
test('synthetic-cannot-pass-real-cli-mode', () => {
  const d = baseline();
  assert.throws(() => verifyRun(bytes(d.rows), Buffer.from(JSON.stringify({ ...d.supervisor, reportSha256: sha(bytes(d.rows)) }))), /Synthetic/);
});
test('post-expiry-stale-event', () => {
  const d = baseline(2100000);
  for (const row of d.rows.filter(r => r.event === 'native.event' && r.elapsedMs > 1800020)) {
    // Keep exact event identities/sequences and Web's self-reported late flags,
    // but move every referenced late native event before the expiry boundary.
    row.elapsedMs = 1800019;
  }
  d.rows.sort((a, b) => a.elapsedMs - b.elapsedMs).forEach((r, i) => { r.seq = i + 1; });
  assert.throws(() => check(d), /Missing real post-original-expiry/);
});
test('fourth-renewal-attempt', () => {
  const d = baseline(2100000, true); let changed = false;
  for (const row of d.rows.filter(r => r.event === 'web.record')) {
    const v = JSON.parse(row.dataJson);
    if (!changed && v.kind === 'protocol' && v.observations.some(o => o.method === 'bridge.session.renew')) {
      const req = v.observations.find(o => o.method === 'bridge.session.renew' && !('ok' in o) && !o.transportError);
      v.observations.splice(0, 0, { ...req, requestId: 'extra', observedElapsedMs: req.observedElapsedMs },
        { method: req.method, requestId: 'extra', ok: false, errorCode: 'E_BUSY', observedElapsedMs: req.observedElapsedMs });
      row.dataJson = JSON.stringify(v); changed = true;
    }
  }
  assert.throws(() => check(d), /exceeds three attempts/);
});
test('native-real-duration-shortened', () => {
  const d = baseline();
  for (const row of d.rows) if (row.elapsedMs > 80) row.elapsedMs = 80 + Math.floor((row.elapsedMs - 80) / 2);
  assert.throws(() => check(d), /Native ready-to-complete duration is too short/);
});
test('missing-response-not-silently-skipped', () => {
  const d = baseline();
  for (const row of d.rows.filter(r => r.event === 'web.record')) {
    const v = JSON.parse(row.dataJson);
    if (v.kind === 'protocol') {
      v.observations = v.observations.filter(o => !(o.method === 'probe.describe' && o.ok === true));
      row.dataJson = JSON.stringify(v);
    }
  }
  assert.throws(() => check(d), /Unpaired protocol request/);
});
test('duplicate-response-not-silently-replaced', () => {
  const d = baseline();
  for (const row of d.rows.filter(r => r.event === 'web.record')) {
    const v = JSON.parse(row.dataJson);
    if (v.kind === 'protocol') {
      const at = v.observations.findIndex(o => o.method === 'task.cancel' && o.ok === true);
      if (at !== -1) { v.observations.splice(at, 0, { ...v.observations[at] }); row.dataJson = JSON.stringify(v); }
    }
  }
  assert.throws(() => check(d), /duplicate\/mismatched protocol response/);
});
test('retryable-transport-is-not-a-success-ack', () => {
  const d = baseline(2100000, true);
  eachObservation(d, o => {
    if (o.method === 'bridge.session.renew' && o.ok === true) { delete o.ok; o.transportError = true; o.errorCode = 'E_TRANSPORT'; o.operationId = 'synthetic-renew-2'; }
  });
  assert.throws(() => check(d), /Lease lifetime\/generation differs/);
});
test('renewal-generation-skip', () => {
  const d = baseline(2100000);
  eachObservation(d, o => { if (o.method === 'bridge.session.renew' && o.ok) o.generation = 3; });
  assert.throws(() => check(d), /Renewal identity or generation differs/);
});
test('retry-operation-identity-changed-before-recovery', () => {
  const d = baseline(2100000, true); let seen = 0;
  eachObservation(d, o => {
    if (o.method === 'bridge.session.renew' && !('ok' in o) && !o.transportError && ++seen === 2) o.operationId = 'different-operation';
  });
  assert.throws(() => check(d), /before previous operation recovered/);
});
test('payload-manifest-duplicate', () => {
  const f = { path: 'resources/index.html', bytes: 1, sha256: 'a'.repeat(64) };
  assert.throws(() => manifest([f, { ...f, path: 'Resources/INDEX.html' }], 'test'), /duplicate/);
});
test('payload-manifest-traversal', () => {
  assert.throws(() => manifest([{ path: '../secret', bytes: 1, sha256: 'a'.repeat(64) }], 'test'), /invalid/);
});
test('payload-manifest-invalid-byte-count', () => {
  assert.throws(() => manifest([{ path: 'sample', bytes: '1', sha256: 'a'.repeat(64) }], 'test'), /invalid/);
});
test('payload-and-freeze-binding', () => {
  const runtimePath = path.join(suite, 'synthetic-runtime'); fs.mkdirSync(runtimePath);
  const payload = Buffer.from('Synthetic parser payload; never executable.');
  const file = path.join(runtimePath, 'sample.txt'); fs.writeFileSync(file, payload, { flag: 'wx' });
  const payloadBefore = [{ path: 'sample.txt', bytes: payload.length, sha256: sha(payload) }];
  const payloadSha256 = sha(Buffer.from(JSON.stringify(payloadBefore, null, 2) + '\n'));
  const buildRecordPath = path.join(suite, 'synthetic-build.json');
  const build = { schema: 'soui.real-renewal-build/v1', architecture: 'x86', fixtureRevision: 2,
    parent: {freezeSha256: 'b58de0c44a2ae96a12aa144f4d8607b800415c40b1311591e05b91f67486faef'},
    sourceSha256: 'b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07',
    buildId: 'synthetic-build', sdkBuildId: '00000000-0000-0000-0000-000000000001', installedSdkSha256: 'a'.repeat(64),
    nativeBuild: 'passed', webBuild: 'passed', selfCheck: 'passed', sourceUnchanged: true, guiExecuted: false,
    realRenewalVerified: false, releaseReady: false, payload: { sha256: payloadSha256, bytes: payload.length, files: payloadBefore } };
  const buildBytes = Buffer.from(JSON.stringify(build, null, 2) + '\n');
  fs.writeFileSync(buildRecordPath, buildBytes, { flag: 'wx' });
  const frozen = { syntheticOnly: true, schema: 'soui.real-renewal-preparation/v1', status: 'compiled-inputs-frozen', version: '0.3.0-preview.15', fixtureRevision: 2,
    parent: {freezeSha256: 'b58de0c44a2ae96a12aa144f4d8607b800415c40b1311591e05b91f67486faef'},
    sourceInputSha256: 'b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07',
    runtimes: { x86: { runtimePath, payload: payloadBefore, payloadSha256, buildRecordPath, buildRecordSha256: sha(buildBytes),
      buildId: 'synthetic-build', sdkBuildId: '00000000-0000-0000-0000-000000000001' } } };
  const freezePath = path.join(suite, 'synthetic-freeze.json'), freeze = Buffer.from(JSON.stringify(frozen) + '\n');
  fs.writeFileSync(freezePath, freeze, { flag: 'wx' });
  const reportPath = path.join(suite, 'synthetic-smoke.ndjson');
  const supervisor = { architecture: 'x86', runtimePath, freezePath, freezeSha256: sha(freeze), reportPath,
    payloadBefore, payloadAfter: payloadBefore, runtimeDiagnostics: [] };
  assert.equal(bindPayload(supervisor).payloadFiles, 1);
  assert.throws(() => bindPayload({ ...supervisor, freezeSha256: 'b'.repeat(64) }), /Freeze file hash/);
  assert.throws(() => bindPayload({ ...supervisor, payloadAfter: [{ ...payloadBefore[0], bytes: 1 }] }), /Before\/after/);
  assert.throws(() => bindPayload({ ...supervisor, architecture: 'x64' }), /Compiled runtime\/build identity/);
  const alienFreezePath = path.join(suite, 'synthetic-alien-freeze.json');
  const alienFreeze = Buffer.from(JSON.stringify({ ...frozen, sourceInputSha256: 'a'.repeat(64) }));
  fs.writeFileSync(alienFreezePath, alienFreeze, { flag: 'wx' });
  assert.throws(() => bindPayload({ ...supervisor, freezePath: alienFreezePath, freezeSha256: sha(alienFreeze) }), /Freeze schema\/source\/architecture/);
  const changedFreezePath = path.join(suite, 'synthetic-changed-freeze.json');
  const changedFreeze = Buffer.from(JSON.stringify({ ...frozen, runtimes: { x86: {
    ...frozen.runtimes.x86, payload: [{ ...payloadBefore[0], bytes: 1 }] } } }));
  fs.writeFileSync(changedFreezePath, changedFreeze, { flag: 'wx' });
  assert.throws(() => bindPayload({ ...supervisor, freezePath: changedFreezePath, freezeSha256: sha(changedFreeze) }), /Frozen payload differs/);
  const preparedPath = path.join(suite, 'synthetic-prepared.json');
  const prepared = Buffer.from(JSON.stringify({ ...frozen, status: 'prepared' }));
  fs.writeFileSync(preparedPath, prepared, { flag: 'wx' });
  assert.throws(() => bindPayload({ ...supervisor, freezePath: preparedPath, freezeSha256: sha(prepared) }), /Freeze schema\/source\/architecture/);
  // Mutations are confined to this explicitly synthetic self-test-only folder.
  fs.writeFileSync(file, 'mutated synthetic data');
  assert.throws(() => bindPayload(supervisor), /Live runtime/);
});
test('supervisor-report-path-binding', () => {
  const d = baseline(), report = bytes(d.rows);
  assert.throws(() => verifyRun(report, Buffer.from(JSON.stringify({ ...d.supervisor, reportSha256: sha(report) })),
    { synthetic: true, reportPath: path.join(suite, 'different.ndjson') }), /report path differs/);
});
test('missing-supervisor-report-hash', () => {
  const d = baseline(); assert.throws(() => verifyRun(bytes(d.rows), Buffer.from(JSON.stringify(d.supervisor)), { synthetic: true }), /hash missing/);
});
test('raw-synthetic-input-bytes-unchanged', () => {
  const reportPath = path.join(suite, 'synthetic-smoke.ndjson');
  const supervisorPath = path.join(suite, 'synthetic-smoke.supervisor.json');
  const report = fs.readFileSync(reportPath), supervisor = fs.readFileSync(supervisorPath);
  verifyRun(report, supervisor, { synthetic: true, reportPath });
  assert.deepEqual(fs.readFileSync(reportPath), report);
  assert.deepEqual(fs.readFileSync(supervisorPath), supervisor);
});
let diagnosticFixtureIndex = 0;
function diagnosticFixture(present = true) {
  const directory = path.join(suite, 'diagnostics-' + (++diagnosticFixtureIndex));
  const runtimePath = path.join(directory, 'runtime'); fs.mkdirSync(runtimePath, { recursive: true });
  fs.writeFileSync(path.join(runtimePath, 'sample.txt'), 'Synthetic parser payload; never executable.', { flag: 'wx' });
  const frozen = JSON.parse(fs.readFileSync(path.join(suite, 'synthetic-freeze.json'), 'utf8'));
  frozen.runtimes.x86.runtimePath = runtimePath;
  const freeze = Buffer.from(JSON.stringify(frozen)), freezePath = path.join(directory, 'freeze.json');
  fs.writeFileSync(freezePath, freeze, { flag: 'wx' });
  const supervisor = { architecture: 'x86', runtimePath, freezePath, freezeSha256: sha(freeze),
    reportPath: path.join(suite, 'synthetic-smoke.ndjson'), payloadBefore: frozen.runtimes.x86.payload,
    payloadAfter: frozen.runtimes.x86.payload, runtimeDiagnostics: [] };
  const navigation = Buffer.from([0xef, 0xbb, 0xbf, 0x53, 0x59, 0x4e, 0x54, 0x48, 0x0d, 0x0a, 0x00, 0xff]);
  const rawPath = path.join(runtimePath, 'source-navigation.log'), archivePath = path.join(directory, 'navigation.raw.log');
  if (present) {
    fs.writeFileSync(rawPath, navigation, { flag: 'wx' }); fs.writeFileSync(archivePath, navigation, { flag: 'wx' });
    supervisor.runtimeDiagnostics.push({ path: 'source-navigation.log', bytes: navigation.length, sha256: sha(navigation), archivePath });
  }
  return { supervisor, directory, runtimePath, rawPath, archivePath, navigation };
}
test('diagnostic-absent-empty-array', () => {
  const f = diagnosticFixture(false); assert.deepEqual(bindPayload(f.supervisor).runtimeDiagnostics, []);
});
test('diagnostic-exact-root-file-and-byte-identical-archive', () => {
  const f = diagnosticFixture(), before = fs.readFileSync(f.rawPath), archiveBefore = fs.readFileSync(f.archivePath);
  const result = bindPayload(f.supervisor);
  assert.equal(result.payloadFiles, 1); assert.deepEqual(result.runtimeDiagnostics, f.supervisor.runtimeDiagnostics);
  assert.deepEqual(fs.readFileSync(f.rawPath), before); assert.deepEqual(fs.readFileSync(f.archivePath), archiveBefore);
});
test('diagnostic-required-array', () => {
  const f = diagnosticFixture(false); delete f.supervisor.runtimeDiagnostics;
  assert.throws(() => bindPayload(f.supervisor), /runtimeDiagnostics must be/);
});
test('diagnostic-exists-but-unlisted', () => {
  const f = diagnosticFixture(); f.supervisor.runtimeDiagnostics = [];
  assert.throws(() => bindPayload(f.supervisor), /presence differs/);
});
test('diagnostic-listed-but-absent', () => {
  const f = diagnosticFixture(false);
  f.supervisor.runtimeDiagnostics = [{ path: 'source-navigation.log', bytes: 1, sha256: 'a'.repeat(64), archivePath: f.archivePath }];
  assert.throws(() => bindPayload(f.supervisor), /presence differs/);
});
test('diagnostic-wrong-hash', () => {
  const f = diagnosticFixture(); f.supervisor.runtimeDiagnostics[0].sha256 = 'a'.repeat(64);
  assert.throws(() => bindPayload(f.supervisor), /bytes\/hash differ/);
});
test('diagnostic-wrong-byte-length', () => {
  const f = diagnosticFixture(); --f.supervisor.runtimeDiagnostics[0].bytes;
  assert.throws(() => bindPayload(f.supervisor), /bytes\/hash differ/);
});
test('diagnostic-archive-different-bytes', () => {
  const f = diagnosticFixture(); fs.writeFileSync(f.archivePath, 'different synthetic archive');
  assert.throws(() => bindPayload(f.supervisor), /archive bytes differ/);
});
test('diagnostic-self-archive-inside-runtime-rejected', () => {
  const f = diagnosticFixture(); f.supervisor.runtimeDiagnostics[0].archivePath = f.rawPath;
  assert.throws(() => bindPayload(f.supervisor), /archive must be outside/);
});
test('diagnostic-other-log-name-not-allowed', () => {
  const f = diagnosticFixture(); f.supervisor.runtimeDiagnostics[0].path = 'another.log';
  assert.throws(() => bindPayload(f.supervisor), /descriptor differs/);
});
test('diagnostic-duplicate-descriptor-rejected', () => {
  const f = diagnosticFixture(); f.supervisor.runtimeDiagnostics.push({ ...f.supervisor.runtimeDiagnostics[0] });
  assert.throws(() => bindPayload(f.supervisor), /runtimeDiagnostics must be/);
});
test('diagnostic-exact-exception-does-not-hide-other-new-log', () => {
  const f = diagnosticFixture(); fs.writeFileSync(path.join(f.runtimePath, 'unlisted.log'), 'new synthetic file', { flag: 'wx' });
  assert.throws(() => bindPayload(f.supervisor), /Live runtime files differ/);
});
test('diagnostic-exception-does-not-apply-in-resources', () => {
  const f = diagnosticFixture(false); fs.mkdirSync(path.join(f.runtimePath, 'resources'));
  fs.writeFileSync(path.join(f.runtimePath, 'resources', 'source-navigation.log'), 'unlisted nested file', { flag: 'wx' });
  assert.throws(() => bindPayload(f.supervisor), /Live runtime files differ/);
});
test('diagnostic-exception-does-not-accept-directory', () => {
  const f = diagnosticFixture(false); fs.mkdirSync(f.rawPath);
  assert.throws(() => bindPayload(f.supervisor), /diagnostic must be a regular file/);
});
test('diagnostic-cannot-be-frozen-in-payload', () => {
  const f = diagnosticFixture(), d = f.supervisor.runtimeDiagnostics[0];
  const payload = [...f.supervisor.payloadBefore, { path: d.path, bytes: d.bytes, sha256: d.sha256 }];
  f.supervisor.payloadBefore = payload; f.supervisor.payloadAfter = payload;
  assert.throws(() => bindPayload(f.supervisor), /cannot be frozen as payload/);
});
const summary = { syntheticOnly: true, actualGuiExecuted: false, actualClockRenewalVerified: false,
  testCount: output.length, passed: output.filter(r => r.passed).length, failed: output.filter(r => !r.passed).length,
  releaseReady: false, benchmarkValid: false, resourceGate: false, cases: output };
fs.writeFileSync(path.join(suite, 'self-test-result.json'), JSON.stringify(summary, null, 2) + '\n', { flag: 'wx' });
console.log(JSON.stringify({ suite, ...summary, cases: undefined }, null, 2));
if (summary.failed) process.exitCode = 1;

