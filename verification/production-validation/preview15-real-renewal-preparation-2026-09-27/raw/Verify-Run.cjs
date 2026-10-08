'use strict';
// Read-only evidence parser. Only the CLI's explicitly named new summary is written.
const fs = require('node:fs');
const path = require('node:path');
const crypto = require('node:crypto');
const { TextDecoder } = require('node:util');
const SCHEMA = 'soui.renewal-probe/v2';
const VERSION = '0.3.0-preview.15';
const SOURCE = 'b0d7cc7d52de3493084e0dec6813f92f9a90a1bbce2f75ad5a62532a6dd87d07';
const ok = (condition, message) => { if (!condition) throw new Error(message); };
const object = v => v !== null && typeof v === 'object' && !Array.isArray(v);
const integer = (v, minimum = 0) => Number.isSafeInteger(v) && v >= minimum;
const text = v => typeof v === 'string' && v.length > 0 && v.length <= 256;
const hash = bytes => crypto.createHash('sha256').update(bytes).digest('hex');
function strictJson(source, label) {
  let parsed;
  try { parsed = JSON.parse(source); } catch { throw new Error(label + ': malformed JSON'); }
  // JSON.parse accepts duplicate member names. Reject them rather than silently
  // allowing a later "exitCode":0 or identity member to replace an earlier one.
  let at = 0;
  const space = () => { while (/\s/.test(source[at] || '') && at < source.length) ++at; };
  function string() {
    const begin = at++;
    while (at < source.length) {
      if (source[at] === '\\') { at += 2; continue; }
      if (source[at++] === '"') return JSON.parse(source.slice(begin, at));
    }
    throw new Error(label + ': unterminated string');
  }
  function value() {
    space();
    if (source[at] === '"') { string(); return; }
    if (source[at] === '{') {
      ++at; space(); const names = new Set();
      if (source[at] === '}') { ++at; return; }
      for (;;) {
        space(); const name = string();
        ok(!names.has(name), label + ': duplicate JSON member ' + name); names.add(name);
        space(); ++at; value(); space();
        if (source[at++] === '}') return;
      }
    }
    if (source[at] === '[') {
      ++at; space(); if (source[at] === ']') { ++at; return; }
      for (;;) { value(); space(); if (source[at++] === ']') return; }
    }
    while (at < source.length && !/[\s,\]}]/.test(source[at])) ++at;
  }
  value(); space(); ok(at === source.length, label + ': trailing data');
  return parsed;
}
function decode(bytes, label) {
  try { return new TextDecoder('utf-8', { fatal: true }).decode(bytes).replace(/^\uFEFF/, ''); }
  catch { throw new Error(label + ': invalid UTF-8'); }
}
function parseReport(bytes) {
  ok(bytes.length > 0 && bytes.length <= 256 * 1024 * 1024, 'Report size out of bounds');
  const source = decode(bytes, 'report');
  ok(source.endsWith('\n'), 'Report is truncated: final newline missing');
  return source.slice(0, -1).split('\n').map((line, index) => {
    ok(line.trim().length > 0 && Buffer.byteLength(line) <= 65536, 'Blank or oversized NDJSON row');
    return strictJson(line, 'row ' + (index + 1));
  });
}
function fields(value, allowed, label) {
  ok(object(value), label + ': object required');
  for (const key of Object.keys(value)) ok(allowed.includes(key), label + ': unexpected field ' + key);
}
function verifyRows(rows, supervisor, options = {}) {
  const synthetic = options.synthetic === true;
  ok(Array.isArray(rows) && rows.length > 0, 'Missing native rows');
  ok(object(supervisor) && supervisor.exitCode === 0 && supervisor.timedOut === false &&
    supervisor.payloadUnchanged === true, 'Supervisor exit/timeout/payload gate failed');
  ok(supervisor.inputsUnchanged === true && supervisor.supervisionSucceeded === true &&
    supervisor.externalLoadDetected === false && Array.isArray(supervisor.remainingDesktopProcesses) &&
    supervisor.remainingDesktopProcesses.length === 0, 'Supervisor input/isolation/post-run gate failed');
  ok(synthetic ? supervisor.synthetic === true : supervisor.synthetic !== true, 'Synthetic evidence cannot pass as a real run');
  const nativeNames = ['run.started', 'host.mounted', 'probe.describe', 'probe.ready', 'web.record',
    'task.invoked', 'task.stopped', 'native.event', 'resource.sample', 'probe.finish', 'host.destroyed',
    'runtime.shutdown', 'run.finished', 'bridge.bootstrap.begin', 'bridge.bootstrap.bound', 'session.shutdown'];
  let priorTime = -1;
  for (let i = 0; i < rows.length; ++i) {
    const row = rows[i];
    ok(object(row) && row.schema === SCHEMA && row.seq === i + 1, 'Native schema/continuous sequence differs');
    ok(integer(row.elapsedMs) && row.elapsedMs >= priorTime, 'Native monotonic clock regressed'); priorTime = row.elapsedMs;
    ok(nativeNames.includes(row.event), 'Unexpected/failure native event: ' + row.event);
    ok(!('token' in row), 'Credential field in native evidence');
  }
  const select = name => rows.filter(row => row.event === name);
  const one = name => { const found = select(name); ok(found.length === 1, name + ' must occur exactly once'); return found[0]; };
  const start = one('run.started'), mounted = one('host.mounted'), describe = one('probe.describe');
  const ready = one('probe.ready'), finish = one('probe.finish'), destroyed = one('host.destroyed');
  const shutdown = one('runtime.shutdown'), end = one('run.finished');
  const bootstrapBegin = one('bridge.bootstrap.begin'), bootstrapBound = one('bridge.bootstrap.bound');
  const longRun = start.durationMs !== 30000;
  ok([30000, 2100000, 28800000].includes(start.durationMs) && start.version === VERSION &&
    ['x86', 'x64'].includes(start.architecture) && text(start.runId) && integer(start.pid, 1), 'Invalid run identity/duration/version');
  ok(synthetic ? start.synthetic === true : start.synthetic !== true, 'Synthetic native sample is not real-clock evidence');
  ok(rows[0] === start && rows[rows.length - 1] === end && start.realRenewalRequired === longRun, 'Run boundaries or renewal mode differ');
  ok(start.seq < mounted.seq && mounted.seq < describe.seq && describe.seq < ready.seq &&
    ready.seq < finish.seq && finish.seq < destroyed.seq && destroyed.seq < shutdown.seq && shutdown.seq < end.seq, 'Native lifecycle order differs');
  ok(mounted.mountCount === 1 && mounted.reloadCount === 0 && ready.count === 1, 'Mount/reload/ready count differs');
  ok(ready.elapsedMs <= 60000 && end.elapsedMs <= start.durationMs + 120000, 'Native readiness/overall deadline exceeded');
  ok(destroyed.ok === true && destroyed.code === '' && destroyed.activeScope === false &&
    shutdown.ok === true && shutdown.code === '', 'Cleanup/authorization revocation was not confirmed');
  ok(end.exitCode === 0 && end.readyCount === 1 && end.longInvocations === 1 && end.resultInvocations === 1 &&
    end.bootstrapCount === 1 && end.boundShutdowns === 1 &&
    end.realRenewalVerified === false && end.releaseReady === false, 'Final native state is not clean');
  for (const [key, actual] of [['pid', start.pid], ['durationMs', start.durationMs], ['architecture', start.architecture]]) {
    ok(supervisor[key] === actual, 'Supervisor ' + key + ' missing/mismatch');
  }
  ok(text(describe.sessionId) && describe.origin === 'app://installed-renewal-probe' && describe.sameAuthorization === true, 'Missing bound authorization identity');
  const session = describe.sessionId, origin = describe.origin;
  const envelope = ['schema', 'seq', 'elapsedMs', 'event'];
  fields(bootstrapBegin, [...envelope, 'bootstrapCount', 'phase'], 'Native bootstrap begin');
  fields(bootstrapBound, [...envelope, 'bootstrapCount', 'phase', 'sessionId', 'origin'], 'Native bootstrap binding');
  ok(start.seq < bootstrapBegin.seq && bootstrapBegin.seq < bootstrapBound.seq && bootstrapBound.seq < describe.seq &&
    bootstrapBegin.bootstrapCount === 1 && bootstrapBegin.phase === 'bootstrap-in-flight' &&
    bootstrapBound.bootstrapCount === 1 && bootstrapBound.phase === 'bound-before-describe' &&
    bootstrapBound.sessionId === session && bootstrapBound.origin === origin, 'Native bootstrap binding identity/order differs');
  const sessionShutdowns = select('session.shutdown');
  const initialRetirements = sessionShutdowns.filter(row => row.classification === 'pre-bootstrap-lifecycle');
  const boundCloses = sessionShutdowns.filter(row => row.classification === 'bound-close');
  ok(initialRetirements.length <= 1 && boundCloses.length === 1 &&
    sessionShutdowns.length === initialRetirements.length + boundCloses.length, 'Unexpected session retirement/close classification');
  for (const row of sessionShutdowns)
    fields(row, [...envelope, 'sessionId', 'phase', 'classification', 'bootstrapCount', 'bound', 'readyCount'], 'Native session shutdown');
  for (const row of initialRetirements) {
    ok(text(row.sessionId) && row.sessionId !== session && row.phase === 'before-bootstrap' &&
      row.bootstrapCount === 0 && row.bound === false && row.readyCount === 0 &&
      start.seq < row.seq && row.seq < bootstrapBegin.seq, 'Initial retirement is outside the strict pre-bootstrap phase');
  }
  const boundClose = boundCloses[0];
  ok(boundClose.sessionId === session && boundClose.phase === 'closing-bound' && boundClose.bootstrapCount === 1 &&
    boundClose.bound === true && boundClose.readyCount === 1 && finish.seq < boundClose.seq && boundClose.seq < destroyed.seq,
    'Bound document session was not closed exactly once after finish');
  ok(end.prebootstrapRetirements === initialRetirements.length && end.shutdownSessions === sessionShutdowns.length,
    'Final retirement counts differ');
  for (const row of rows.filter(row => ['probe.describe', 'probe.ready', 'probe.finish', 'web.record'].includes(row.event))) {
    ok(row.sessionId === session && row.origin === origin && row.sameAuthorization === true, 'Native authorization scope changed');
  }
  const invoked = select('task.invoked'), stopped = select('task.stopped');
  ok(invoked.length === 2 && stopped.length === 2, 'Exactly two actual task invocations and stops are required');
  for (const kind of ['probe.long', 'probe.result']) {
    const calls = invoked.filter(row => row.kind === kind), stops = stopped.filter(row => row.kind === kind);
    ok(calls.length === 1 && calls[0].count === 1 && calls[0].sessionId === session &&
      calls[0].operationId === start.runId + (kind === 'probe.long' ? ':long' : ':result') &&
      describe.seq < calls[0].seq && calls[0].seq < ready.seq, 'Task invocation identity/count/order differs');
    ok(stops.length === 1 && stops[0].reason === (kind === 'probe.long' ? 'cancelled' : 'completed') &&
      calls[0].seq < stops[0].seq && stops[0].seq < finish.seq, 'Task did not stop through its required path');
  }
  const webRows = select('web.record').map(row => {
    ok(typeof row.dataJson === 'string' && Buffer.byteLength(row.dataJson) <= 16384, 'Invalid/oversized durable Web record');
    const data = strictJson(row.dataJson, 'Web record ' + row.seq);
    ok(object(data) && ['begin', 'protocol', 'heartbeat', 'complete'].includes(data.kind), 'Unknown or failed Web record');
    return { row, data };
  });
  const webOne = kind => { const a = webRows.filter(r => r.data.kind === kind); ok(a.length === 1, 'Web ' + kind + ' must occur exactly once'); return a[0]; };
  const begin = webOne('begin'), complete = webOne('complete');
  const fixed = begin.data;
  ok(text(fixed.documentNonce) && text(fixed.longTaskId) && text(fixed.resultTaskId) && text(fixed.subscriptionId) &&
    fixed.longTaskId !== fixed.resultTaskId && fixed.sessionId === session && fixed.initialGeneration === 1 &&
    fixed.durationMs === start.durationMs, 'Initial Web identities differ');
  ok(describe.seq < begin.row.seq && begin.row.seq < ready.seq && ready.seq < complete.row.seq &&
    complete.row.seq < finish.seq, 'Begin/ready/complete/finish order differs');
  ok(complete.row.elapsedMs - ready.elapsedMs >= start.durationMs, 'Native ready-to-complete duration is too short');
  for (const item of webRows) {
    ok(item.row.seq < finish.seq, 'Web evidence after finish');
    if (item.data.kind !== 'protocol') {
      for (const key of ['documentNonce', 'sessionId', 'longTaskId', 'resultTaskId', 'subscriptionId'])
        ok(item.data[key] === fixed[key], 'Web identity changed: ' + key);
    }
  }
  let lastRecord = ready.elapsedMs;
  for (const item of webRows.filter(r => r.row.seq > ready.seq)) {
    ok(item.row.elapsedMs - lastRecord <= 30000, 'Native post-ready record silence exceeds 30 seconds');
    lastRecord = item.row.elapsedMs;
  }
  const requests = new Map(), pairs = [], renewals = new Map();
  const observationFields = ['method', 'requestId', 'operationId', 'ok', 'errorCode', 'sessionId', 'taskId',
    'subscriptionId', 'generation', 'kind', 'origin', 'state', 'sequence', 'resultValue', 'accepted',
    'duplicate', 'cancelRequested', 'removed', 'transportError', 'observedElapsedMs'];
  const methods = ['bridge.bootstrap', 'bridge.session.renew', 'bridge.probe.report', 'probe.describe',
    'probe.ready', 'task.start', 'task.query', 'task.cancel', 'task.subscribe', 'task.unsubscribe'];
  let order = 0, generation = 0, priorLease = 0, previousRenewOp = '', observedClock = -1;
  let priorHeartbeat, priorHeartbeatNativeMs = ready.elapsedMs, heartbeats = 0, lastQueryOrder = -1, crossedExpiry = false;
  const nativeEvents = new Map(); let nativeEventSequence = 0;
  for (const row of select('native.event')) {
    ok(typeof row.dataJson === 'string' && Buffer.byteLength(row.dataJson) <= 16384, 'Invalid native event JSON');
    const event = strictJson(row.dataJson, 'native event');
    ok(object(event) && event.sessionId === session && event.taskId === fixed.longTaskId &&
      event.eventName === 'task.progress' && integer(event.sequence, 1) && event.sequence > nativeEventSequence,
      'Native progress identity/sequence changed');
    nativeEventSequence = event.sequence; nativeEvents.set(event.sequence, { row, event });
  }
  const verifyLease = lease => {
    ok(object(lease) && integer(lease.generation, 1) && lease.generation >= priorLease &&
      lease.generation <= generation && integer(lease.expiresInMs, 1) && lease.expiresInMs <= 1800000 &&
      integer(lease.renewAfterMs) && lease.renewAfterMs <= 1500000 && lease.renewAfterMs <= lease.expiresInMs,
      'Lease lifetime/generation differs');
    priorLease = lease.generation;
  };
  function addObservation(observation, row) {
    fields(observation, observationFields, 'Protocol observation');
    ok(methods.includes(observation.method) && text(observation.requestId), 'Unknown protocol method/request ID');
    ok(typeof observation.observedElapsedMs === 'number' && Number.isFinite(observation.observedElapsedMs) &&
      observation.observedElapsedMs >= observedClock && observation.observedElapsedMs >= 0,
      'Missing/regressing observation clock'); observedClock = observation.observedElapsedMs;
    const response = 'ok' in observation || 'transportError' in observation;
    if (!response) {
      ok(!requests.has(observation.requestId), 'Duplicate protocol request ID');
      if (observation.method !== 'bridge.bootstrap')
        ok(observation.sessionId === session && observation.origin === origin, 'Protocol request authorization identity changed');
      const entry = { request: observation, row, order: ++order, response: null };
      requests.set(observation.requestId, entry);
      if (observation.method === 'bridge.session.renew') {
        ok(typeof observation.operationId === 'string' && /^[\x20-\x7e]{1,128}$/.test(observation.operationId), 'Invalid renewal operation ID');
        let group = renewals.get(observation.operationId);
        if (!group) {
          if (previousRenewOp) ok(renewals.get(previousRenewOp).success, 'New renewal started before previous operation recovered');
          group = { entries: [], success: false, generation: null }; renewals.set(observation.operationId, group);
          previousRenewOp = observation.operationId;
        }
        ok(previousRenewOp === observation.operationId && group.entries.length < 3, 'Renewal operation reused or exceeds three attempts');
        group.entries.push(entry);
      }
      return;
    }
    const entry = requests.get(observation.requestId);
    ok(entry && !entry.response && entry.request.method === observation.method, 'Orphan/duplicate/mismatched protocol response');
    entry.response = observation; entry.responseRow = row; entry.responseOrder = ++order; pairs.push(entry);
    if (observation.transportError === true) {
      ok(!('ok' in observation) && observation.method === 'bridge.session.renew' &&
        observation.errorCode === 'E_TRANSPORT' && observation.operationId === entry.request.operationId,
        'Invalid transport-error observation');
      return;
    }
    ok(typeof observation.ok === 'boolean' && !('transportError' in observation), 'Invalid response status');
    if (!observation.ok) {
      ok(observation.method === 'bridge.session.renew' &&
        ['E_BUSY', 'E_TIMEOUT', 'E_TRANSPORT', 'E_RENEWAL_TIMEOUT'].includes(observation.errorCode), 'Unrecovered business/nonretryable protocol error');
      return;
    }
    if (observation.method === 'bridge.bootstrap') {
      ok(generation === 0 && observation.generation === 1 && observation.sessionId === session &&
        observation.origin === origin, 'Bootstrap identity/generation differs'); generation = 1;
    } else if (observation.method === 'bridge.session.renew') {
      const group = renewals.get(entry.request.operationId);
      ok(observation.sessionId === session && observation.origin === origin &&
        integer(observation.generation, 2) && (group.success ? observation.generation === group.generation :
          observation.generation === generation + 1), 'Renewal identity or generation differs');
      ok(previousRenewOp === entry.request.operationId, 'Late renewal from an older operation');
      if (!group.success) { group.success = true; group.generation = observation.generation; generation = observation.generation; }
    }
  }
  for (const item of webRows) {
    const { row, data } = item;
    if (data.kind === 'protocol') {
      ok(Array.isArray(data.observations) && data.observations.length >= 1 && data.observations.length <= 12 &&
        JSON.stringify(data.observations).length <= 4000, 'Protocol batch bounds/truncation differ');
      for (const observation of data.observations) addObservation(observation, row);
      continue;
    }
    if (data.kind === 'begin') continue;
    ok(data.bootstrapCount === 1 && data.startCount === 2 && data.subscribeCount === 1, 'Web bootstrap/task/subscription count changed');
    verifyLease(data.lease);
    ok(integer(data.elapsedMs) && data.elapsedMs <= row.elapsedMs - ready.elapsedMs + 1, 'Web elapsed exceeds native ready elapsed');
    if (data.kind === 'heartbeat') {
      ok(row.seq > ready.seq && row.seq < complete.row.seq, 'Heartbeat outside active run');
      ok(row.elapsedMs - priorHeartbeatNativeMs <= 30000, 'Heartbeat observation gap exceeds 30 seconds');
      priorHeartbeatNativeMs = row.elapsedMs;
      ok(integer(data.eventCount, 1) && integer(data.lastEventSequence, 1) && integer(data.querySequence) &&
        integer(data.lastEventElapsedMs) && data.lastEventElapsedMs <= data.elapsedMs &&
        data.elapsedMs - data.lastEventElapsedMs <= 15000, 'Heartbeat progress/silence evidence differs');
      if (priorHeartbeat) ok(data.elapsedMs > priorHeartbeat.elapsedMs && data.eventCount > priorHeartbeat.eventCount &&
        data.lastEventSequence > priorHeartbeat.lastEventSequence && data.querySequence >= priorHeartbeat.querySequence, 'Heartbeat did not advance');
      const received = nativeEvents.get(data.lastEventSequence);
      ok(received && received.row.seq < row.seq, 'Heartbeat does not reference a preceding native event');
      const deliveredCount = [...nativeEvents.values()].filter(v => v.event.sequence <= data.lastEventSequence).length;
      ok(data.eventCount <= deliveredCount, 'Web reports more received events than native emitted');
      const recent = pairs.filter(p => p.order > lastQueryOrder && p.responseOrder <= order && p.request.method === 'task.query' && p.response.ok);
      const findQuery = id => recent.filter(p => p.request.taskId === id).at(-1);
      const l = findQuery(fixed.longTaskId), r = findQuery(fixed.resultTaskId);
      ok(l && r && l.response.taskId === fixed.longTaskId && l.response.state === 'running' &&
        l.response.sequence === data.querySequence && r.response.taskId === fixed.resultTaskId &&
        r.response.state === 'succeeded' && r.response.resultValue === 42, 'Heartbeat lacks two fresh actual successful queries');
      if (received.row.elapsedMs > describe.elapsedMs + 1800000 && data.lease.generation >= 2) crossedExpiry = true;
      priorHeartbeat = data; ++heartbeats; lastQueryOrder = order;
    }
  }
  ok(heartbeats > 0 && priorHeartbeat.elapsedMs >= start.durationMs, 'Final heartbeat did not reach the configured duration');
  ok(complete.row.elapsedMs - priorHeartbeatNativeMs <= 30000, 'Final cleanup observation gap exceeds 30 seconds');
  const byMethod = method => [...requests.values()].filter(p => p.request.method === method);
  const exactly = (method, count) => { const p = byMethod(method); ok(p.length === count, 'Protocol ' + method + ' count differs'); return p; };
  for (const method of ['bridge.bootstrap', 'probe.describe', 'bridge.probe.report', 'probe.ready', 'task.subscribe', 'task.cancel', 'task.unsubscribe']) exactly(method, 1);
  for (const entry of requests.values()) ok(entry.response, 'Unpaired protocol request');
  for (const entry of byMethod('task.query')) {
    ok([fixed.longTaskId, fixed.resultTaskId].includes(entry.request.taskId) && entry.response.ok === true &&
      entry.response.taskId === entry.request.taskId && integer(entry.response.sequence) &&
      ['queued', 'running', 'succeeded', 'cancel-pending', 'cancelled'].includes(entry.response.state),
      'Query response identity/state/sequence differs');
  }
  for (const taskId of [fixed.longTaskId, fixed.resultTaskId]) {
    let last = -1;
    for (const entry of pairs.filter(p => p.request.method === 'task.query' && p.request.taskId === taskId)) {
      ok(entry.response.sequence >= last, 'Actual task query sequence regressed'); last = entry.response.sequence;
    }
  }
  const renewalTiming = [];
  for (const [op, group] of renewals) {
    ok(group.success, 'Renewal operation did not recover: ' + op);
    const first = group.entries[0].request.observedElapsedMs;
    const success = group.entries.filter(e => e.response.ok)
      .reduce((a, b) => a.response.observedElapsedMs <= b.response.observedElapsedMs ? a : b);
    const successObservedSpanMs = success.response.observedElapsedMs - first;
    const allOutcomesObservedSpanMs = Math.max(...group.entries.map(e => e.response.observedElapsedMs)) - first;
    // Native/UI scheduling and observer execution can delay observation. Keep
    // the actual span and any 25s exceedance; this is not the SDK timer test.
    renewalTiming.push({ operationId: op, attempts: group.entries.length, successObservedSpanMs,
      allOutcomesObservedSpanMs, successObservationExceeds25000Ms: successObservedSpanMs > 25000 });
  }
  const starts = exactly('task.start', 2);
  for (const kind of ['probe.long', 'probe.result']) {
    const p = starts.filter(e => e.request.kind === kind);
    const id = kind === 'probe.long' ? fixed.longTaskId : fixed.resultTaskId;
    ok(p.length === 1 && p[0].request.operationId === start.runId + (kind === 'probe.long' ? ':long' : ':result') &&
      p[0].response.ok === true && p[0].response.taskId === id && p[0].response.accepted === true &&
      p[0].response.duplicate === false && p[0].responseRow.seq < begin.row.seq,
      'Task start was replaced/replayed or lacks initial actual ACK');
  }
  const sub = byMethod('task.subscribe')[0], cancel = byMethod('task.cancel')[0], unsub = byMethod('task.unsubscribe')[0];
  ok(sub.request.taskId === fixed.longTaskId && sub.response.ok === true &&
    sub.response.subscriptionId === fixed.subscriptionId && sub.responseRow.seq < begin.row.seq, 'Original subscription ACK differs');
  ok(cancel.request.taskId === fixed.longTaskId && cancel.response.ok === true &&
    cancel.response.taskId === fixed.longTaskId && cancel.response.cancelRequested === true &&
    cancel.order > lastQueryOrder, 'Original cancellation ACK missing or before final heartbeat');
  const cancelled = pairs.filter(e => e.request.method === 'task.query' && e.request.taskId === fixed.longTaskId &&
    e.order > cancel.responseOrder && e.response.ok === true && e.response.taskId === fixed.longTaskId && e.response.state === 'cancelled').at(-1);
  ok(cancelled && unsub.order > cancelled.responseOrder && unsub.request.subscriptionId === fixed.subscriptionId &&
    unsub.response.ok === true && unsub.response.removed === true && unsub.response.subscriptionId === fixed.subscriptionId,
    'Cancellation confirmation/original unsubscribe ACK missing');
  const retained = pairs.filter(e => e.request.method === 'task.query' && e.request.taskId === fixed.resultTaskId &&
    e.order > unsub.responseOrder && e.response.ok === true && e.response.taskId === fixed.resultTaskId &&
    e.response.state === 'succeeded' && e.response.resultValue === 42).at(-1);
  ok(retained && retained.responseRow.seq < complete.row.seq && unsub.responseRow.seq < complete.row.seq,
    'Final result retention and unsubscribe evidence were not durable before complete');
  const c = complete.data;
  ok(c.cancelled === true && c.unsubscribed === true && c.unsubscribeCount === 1 && c.resultValue === 42 &&
    c.realRenewalVerified === longRun && c.elapsedMs >= start.durationMs &&
    c.eventCount >= priorHeartbeat.eventCount && c.lastEventSequence >= priorHeartbeat.lastEventSequence &&
    c.querySequence >= priorHeartbeat.querySequence, 'Web complete fields differ');
  const finalEvent = nativeEvents.get(c.lastEventSequence);
  ok(finalEvent && finalEvent.row.seq < complete.row.seq && integer(c.eventCount, 1) &&
    c.eventCount <= [...nativeEvents.keys()].filter(sequence => sequence <= c.lastEventSequence).length,
    'Complete does not reference real retained progress');
  ok(!longRun || (crossedExpiry && c.lease.generation >= (start.durationMs === 28800000 ? 20 : 2) &&
    c.postOriginalExpiryEvent === true && c.postOriginalExpiryHeartbeat === true), 'Missing real post-original-expiry continuity');
  const resources = select('resource.sample');
  ok(resources.length >= 2, 'Missing repeated resource observations');
  for (const row of resources) {
    ok(row.resourceGate === 'not-evaluated', 'Resource sample expanded its conclusion');
    for (const key of ['privateBytes', 'workingSetBytes', 'handles', 'threads', 'gdiObjects', 'userObjects'])
      ok(integer(row[key]), 'Invalid resource observation ' + key);
  }
  return { schema: 'soui.renewal-probe-summary/v2', fixtureRevision: 2, version: VERSION, expectedSourceInputSha256: SOURCE,
    prebootstrapRetirements: initialRetirements.length, boundShutdowns: boundCloses.length,
    runId: start.runId, architecture: start.architecture, durationMs: start.durationMs, nativeRows: rows.length,
    nativeReadyToCompleteMs: complete.row.elapsedMs - ready.elapsedMs, heartbeatCount: heartbeats,
    nativeProgressEvents: nativeEvents.size, finalGeneration: c.lease.generation, renewalOperations: renewals.size,
    maxRenewalAttempts: Math.max(0, ...[...renewals.values()].map(g => g.entries.length)),
    renewalTiming, renewalTimingIsSchedulerObservation: true, exactRecoveryDeadlineVerified: false,
    syntheticOnly: synthetic, evidenceValidated: true, harnessSmokeVerified: !synthetic && !longRun,
    realRenewalVerified: !synthetic && longRun, scenarioEightHourVerified: !synthetic && start.durationMs === 28800000,
    postOriginalExpiryEventCorrelated: crossedExpiry, cleanupConfirmed: true,
    resourceGate: false, benchmarkValid: false, releaseReady: false, productionReady: false, sourceDeliveryVerified: false,
    scope: 'Only this installed public-SDK same-document task/subscription renewal scenario; no Workbench V02, mixed-business, IME or performance acceptance.' };
}
function verifyRun(reportBytes, supervisorBytes, options = {}) {
  const supervisor = strictJson(decode(supervisorBytes, 'supervisor'), 'supervisor');
  ok(supervisor.reportSha256 === hash(reportBytes), 'Supervisor report hash missing/differs');
  ok(typeof supervisor.reportPath === 'string' && path.isAbsolute(supervisor.reportPath), 'Supervisor absolute report path missing');
  if (options.reportPath)
    ok(path.resolve(supervisor.reportPath).toLowerCase() === path.resolve(options.reportPath).toLowerCase(), 'Supervisor report path differs');
  return verifyRows(parseReport(reportBytes), supervisor, options);
}
function manifest(value, label) {
  ok(Array.isArray(value) && value.length > 0 && value.length <= 10000, label + ': missing/bounded file manifest required');
  const seen = new Set();
  for (const file of value) {
    fields(file, ['path', 'bytes', 'sha256'], label);
    ok(typeof file.path === 'string' && file.path.length > 0 && file.path.length <= 1024 &&
      !file.path.includes('\\') && !file.path.includes(':') && !file.path.startsWith('/') &&
      file.path.split('/').every(p => p !== '' && p !== '.' && p !== '..') &&
      integer(file.bytes) && typeof file.sha256 === 'string' && /^[a-f0-9]{64}$/.test(file.sha256),
      label + ': invalid file record');
    ok(!seen.has(file.path.toLowerCase()), label + ': duplicate path'); seen.add(file.path.toLowerCase());
  }
  return value.map(file => ({ path: file.path, bytes: file.bytes, sha256: file.sha256 }))
    .sort((a, b) => a.path < b.path ? -1 : a.path > b.path ? 1 : 0);
}
function bindPayload(supervisor) {
  ok(typeof supervisor.runtimePath === 'string' && path.isAbsolute(supervisor.runtimePath), 'Absolute runtimePath missing');
  ok(typeof supervisor.freezePath === 'string' && path.isAbsolute(supervisor.freezePath) &&
    typeof supervisor.freezeSha256 === 'string' && /^[a-f0-9]{64}$/.test(supervisor.freezeSha256), 'Freeze identity missing');
  const before = manifest(supervisor.payloadBefore, 'payloadBefore'), after = manifest(supervisor.payloadAfter, 'payloadAfter');
  ok(JSON.stringify(before) === JSON.stringify(after), 'Before/after runtime payload differs');
  ok(!before.some(file => file.path.toLowerCase() === 'source-navigation.log'),
    'The root navigation diagnostic cannot be frozen as payload');
  ok(Array.isArray(supervisor.runtimeDiagnostics) && supervisor.runtimeDiagnostics.length <= 1,
    'runtimeDiagnostics must be an empty array or the single known root diagnostic');
  for (const diagnostic of supervisor.runtimeDiagnostics) {
    fields(diagnostic, ['path', 'bytes', 'sha256', 'archivePath'], 'Runtime diagnostic');
    ok(diagnostic.path === 'source-navigation.log' && integer(diagnostic.bytes) &&
      typeof diagnostic.sha256 === 'string' && /^[a-f0-9]{64}$/.test(diagnostic.sha256) &&
      typeof diagnostic.archivePath === 'string' && path.isAbsolute(diagnostic.archivePath),
      'Runtime diagnostic descriptor differs');
  }
  const freezeBytes = fs.readFileSync(supervisor.freezePath);
  ok(hash(freezeBytes) === supervisor.freezeSha256, 'Freeze file hash differs');
  const frozen = strictJson(decode(freezeBytes, 'freeze'), 'freeze');
  ok(object(frozen) && frozen.schema === 'soui.real-renewal-preparation/v1' && frozen.fixtureRevision === 2 &&
    object(frozen.parent) && frozen.parent.freezeSha256 === 'b58de0c44a2ae96a12aa144f4d8607b800415c40b1311591e05b91f67486faef' &&
    frozen.status === 'compiled-inputs-frozen' && frozen.version === VERSION &&
    frozen.sourceInputSha256 === SOURCE && object(frozen.runtimes) &&
    ['x86', 'x64'].includes(supervisor.architecture), 'Freeze schema/source/architecture identity differs');
  const frozenRuntime = frozen.runtimes[supervisor.architecture];
  ok(object(frozenRuntime) && typeof frozenRuntime.runtimePath === 'string' &&
    path.isAbsolute(frozenRuntime.runtimePath) && typeof frozenRuntime.buildId === 'string' &&
    /^[a-z0-9][a-z0-9-]{0,63}$/.test(frozenRuntime.buildId) && typeof frozenRuntime.sdkBuildId === 'string' &&
    /^[a-f0-9]{8}(?:-[a-f0-9]{4}){3}-[a-f0-9]{12}$/i.test(frozenRuntime.sdkBuildId),
    'Compiled runtime/build identity missing from freeze');
  const frozenPayload = manifest(frozenRuntime.payload, 'freeze payload');
  ok(JSON.stringify(frozenPayload) === JSON.stringify(before), 'Frozen payload differs from supervisor');
  ok(frozenRuntime.payloadSha256 === hash(Buffer.from(JSON.stringify(frozenPayload, null, 2) + '\n')),
    'Frozen payload manifest digest differs');
  ok(typeof frozenRuntime.buildRecordPath === 'string' && path.isAbsolute(frozenRuntime.buildRecordPath) &&
    typeof frozenRuntime.buildRecordSha256 === 'string' && /^[a-f0-9]{64}$/.test(frozenRuntime.buildRecordSha256),
    'Frozen build record identity missing');
  const buildBytes = fs.readFileSync(frozenRuntime.buildRecordPath);
  ok(hash(buildBytes) === frozenRuntime.buildRecordSha256, 'Frozen build record hash differs');
  const build = strictJson(decode(buildBytes, 'build record'), 'build record');
  ok(object(build) && build.schema === 'soui.real-renewal-build/v1' &&
    build.fixtureRevision === 2 && build.parent?.freezeSha256 === frozen.parent.freezeSha256 &&
    build.architecture === supervisor.architecture && build.sourceSha256 === SOURCE &&
    build.buildId === frozenRuntime.buildId && build.sdkBuildId === frozenRuntime.sdkBuildId &&
    typeof build.installedSdkSha256 === 'string' && /^[a-f0-9]{64}$/.test(build.installedSdkSha256) &&
    build.nativeBuild === 'passed' && build.webBuild === 'passed' && build.selfCheck === 'passed' &&
    build.sourceUnchanged === true && build.guiExecuted === false &&
    build.realRenewalVerified === false && build.releaseReady === false, 'Frozen build record contract differs');
  ok(object(build.payload) && build.payload.sha256 === frozenRuntime.payloadSha256 &&
    build.payload.bytes === frozenPayload.reduce((sum, file) => sum + file.bytes, 0) &&
    JSON.stringify(manifest(build.payload.files, 'build payload')) === JSON.stringify(frozenPayload),
    'Frozen build payload differs');
  const runtime = fs.realpathSync(supervisor.runtimePath), files = [];
  let navigationDiagnostic;
  ok(fs.realpathSync(frozenRuntime.runtimePath).toLowerCase() === runtime.toLowerCase(), 'Freeze runtime path differs');
  function walk(directory, prefix = '') {
    for (const entry of fs.readdirSync(directory, { withFileTypes: true })) {
      const relative = prefix + entry.name, absolute = path.join(directory, entry.name);
      ok(!fs.lstatSync(absolute).isSymbolicLink(), 'Runtime link cannot be an immutable payload input');
      if (relative === 'source-navigation.log') {
        ok(entry.isFile(), 'Root navigation diagnostic must be a regular file');
        navigationDiagnostic = fs.readFileSync(absolute);
        continue;
      }
      if (entry.isDirectory()) walk(absolute, relative + '/');
      else { ok(entry.isFile(), 'Non-file runtime payload'); const bytes = fs.readFileSync(absolute);
        files.push({ path: relative, bytes: bytes.length, sha256: hash(bytes) }); }
    }
  }
  walk(runtime);
  ok(supervisor.runtimeDiagnostics.length === (navigationDiagnostic === undefined ? 0 : 1),
    'Root navigation diagnostic presence differs from supervisor');
  if (navigationDiagnostic !== undefined) {
    const diagnostic = supervisor.runtimeDiagnostics[0];
    ok(navigationDiagnostic.length === diagnostic.bytes && hash(navigationDiagnostic) === diagnostic.sha256,
      'Root navigation diagnostic bytes/hash differ');
    ok(fs.lstatSync(diagnostic.archivePath).isFile(), 'Diagnostic archive must be a regular file');
    const archive = fs.realpathSync(diagnostic.archivePath), relative = path.relative(runtime, archive);
    ok(relative !== '' && (relative.startsWith('..' + path.sep) || path.isAbsolute(relative)),
      'Diagnostic archive must be outside runtime');
    ok(fs.readFileSync(archive).equals(navigationDiagnostic), 'Diagnostic archive bytes differ from runtime');
  }
  ok(JSON.stringify(manifest(files, 'live runtime')) === JSON.stringify(after), 'Live runtime files differ from supervisor payload');
  const report = fs.realpathSync(supervisor.reportPath), relative = path.relative(runtime, report);
  ok(relative !== '' && (relative.startsWith('..' + path.sep) || path.isAbsolute(relative)), 'Report must be outside the runtime payload');
  return { runtimePath: runtime, payloadFiles: files.length, freezePath: supervisor.freezePath,
    freezeSha256: supervisor.freezeSha256, frozenSourceInputSha256: frozen.sourceInputSha256,
    buildId: frozenRuntime.buildId, sdkBuildId: frozenRuntime.sdkBuildId,
    buildRecordPath: frozenRuntime.buildRecordPath, buildRecordSha256: frozenRuntime.buildRecordSha256,
    payloadSha256: frozenRuntime.payloadSha256, payloadIdentityVerified: true,
    runtimeDiagnostics: supervisor.runtimeDiagnostics, freezeBytes };
}
function main(args) {
  ok(args.length === 6, 'Usage: node Verify-Run.cjs --report ABS.ndjson --supervisor ABS.result.json --output ABS_NEW.summary.json');
  const options = {};
  for (let i = 0; i < args.length; i += 2) {
    ok(['--report', '--supervisor', '--output'].includes(args[i]) && !(args[i] in options) &&
      path.isAbsolute(args[i + 1]), 'Unique known options and absolute paths are required');
    options[args[i]] = args[i + 1];
  }
  ok(Object.keys(options).length === 3, 'Missing input/output option');
  const reportPath = options['--report'], supervisorPath = options['--supervisor'], output = options['--output'];
  ok(!fs.existsSync(output), 'Refuse to overwrite summary');
  const report = fs.readFileSync(reportPath), supervisor = fs.readFileSync(supervisorPath);
  const bound = bindPayload(strictJson(decode(supervisor, 'supervisor'), 'supervisor'));
  const summary = verifyRun(report, supervisor, { reportPath });
  // Re-read the immutable payload after parsing; report/freeze are checked
  // separately below. No evidence files are changed by this verifier.
  bindPayload(strictJson(decode(supervisor, 'supervisor'), 'supervisor'));
  ok(hash(fs.readFileSync(reportPath)) === hash(report) && hash(fs.readFileSync(supervisorPath)) === hash(supervisor),
    'Evidence changed while verifying');
  ok(hash(fs.readFileSync(bound.freezePath)) === hash(bound.freezeBytes), 'Freeze changed while verifying');
  delete bound.freezeBytes;
  Object.assign(summary, bound);
  Object.assign(summary, { reportPath, reportSha256: hash(report), supervisorPath, supervisorSha256: hash(supervisor) });
  fs.writeFileSync(output, JSON.stringify(summary, null, 2) + '\n', { flag: 'wx' });
  process.stdout.write(JSON.stringify(summary, null, 2) + '\n');
}
module.exports = { parseReport, strictJson, verifyRows, verifyRun, manifest, bindPayload };
if (require.main === module) { try { main(process.argv.slice(2)); } catch (error) { console.error('Verification failed: ' + error.message); process.exitCode = 1; } }

