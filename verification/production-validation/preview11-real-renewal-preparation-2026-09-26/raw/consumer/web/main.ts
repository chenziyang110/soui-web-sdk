import { connectSouiNative } from '@soui-sdk';
import type { SouiCoreMethodMap, SouiNativeClientImpl, SouiNativeEvent, SouiNativeWindow, SouiSessionLease, SouiSubscription } from '@soui-sdk';

type Describe = { runId: string; durationMs: number; version: string; architecture: string };
type Methods = SouiCoreMethodMap & {
  'probe.describe': { params: Record<string, never>; result: Describe };
  'probe.ready': { params: Record<string, never>; result: Record<string, never> };
  'probe.record': { params: Record<string, unknown>; result: Record<string, never> };
  'probe.finish': { params: Record<string, never>; result: Record<string, never> };
  'probe.fail': { params: { message: string }; result: Record<string, never> };
};
type Observation = {
  method: string; requestId: string; operationId?: string; ok?: boolean; errorCode?: string;
  sessionId?: string; taskId?: string; subscriptionId?: string; generation?: number;
  kind?: string; origin?: string; state?: string; sequence?: number; resultValue?: number;
  accepted?: boolean; duplicate?: boolean; cancelRequested?: boolean; removed?: boolean;
  transportError?: true;
  observedElapsedMs?: number;
};
type Phase = 'connecting' | 'preparing' | 'running' | 'finishing' | 'awaiting-finish' | 'done' | 'failed';
const target = window as unknown as SouiNativeWindow;
const status = document.getElementById('status')!;
const details = document.getElementById('details')!;
const typing = document.getElementById('typing') as HTMLInputElement;
const interaction = document.getElementById('interaction')!;
const documentNonce = `document-${Date.now()}-${Math.random().toString(36).slice(2)}`;
let phase: Phase = 'connecting';
let client: SouiNativeClientImpl<Methods> | undefined;
let description: Describe | undefined;
let subscription: SouiSubscription | undefined;
let initialHandle: SouiSubscription | undefined;
let sessionId = '', origin = '', longTaskId = '', resultTaskId = '', subscriptionId = '';
let longOperation = '', resultOperation = '';
let bootstrapCount = 0, startCount = 0, subscribeCount = 0, unsubscribeCount = 0;
let observedGeneration = 0, initialGeneration = 0, lastLeaseGeneration = 0, lastRenewOperation = '';
let originalExpiryUpperBound = 0, startedAt = 0, eventCount = 0, lastEventSequence = 0;
let lastEventAt = 0, priorHeartbeatEventCount = 0, querySequence = 0;
let lastRecordedElapsed = -1;
let postOriginalExpiryEvent = false, postOriginalExpiryHeartbeat = false, unsubscribeAcknowledged = false;
let failure = '', finishSent = false, protocolQueue: Observation[] = [];
let inputEvents = 0, compositionEvents = 0, resizeEvents = 0;

const record = (value: unknown): value is Record<string, unknown> => value !== null && typeof value === 'object' && !Array.isArray(value);
class ProbeFailure extends Error {}
function requireThat(value: unknown, message: string): asserts value { if (!value) throw new ProbeFailure(message); }
const bounded = (value: unknown): value is string => typeof value === 'string' && value.length > 0 && value.length <= 256;
const elapsed = () => Math.max(0, Math.floor(performance.now() - startedAt));
const pause = (milliseconds: number) => new Promise<void>(resolve => window.setTimeout(resolve, milliseconds));
function healthy() { requireThat(!failure && phase !== 'failed', failure || 'The run has failed.'); }
function failureCode(error: unknown): string {
  if (record(error) && typeof error.code === 'string' && /^[A-Z][A-Z0-9_]{0,63}$/.test(error.code)) return error.code;
  // Never persist arbitrary exception messages, stacks or native envelopes.
  return 'E_PROBE_CHECK';
}
function enqueue(observation: Observation) {
  if (phase === 'failed' || phase === 'done') return;
  requireThat(protocolQueue.length < 128, 'Protocol observation queue overflow.');
  // Observation-only document clock; never fed into SDK lease or deadlines.
  observation.observedElapsedMs = performance.now();
  protocolQueue.push(observation);
}
function requestObservation(raw: string): Observation {
  const value: unknown = JSON.parse(raw);
  requireThat(record(value) && bounded(value.method) && bounded(value.requestId), 'Invalid observed request metadata.');
  const observation: Observation = { method: value.method, requestId: value.requestId };
  const params = record(value.params) ? value.params : {};
  if (bounded(params.operationId)) observation.operationId = params.operationId;
  if (bounded(params.taskId)) observation.taskId = params.taskId;
  if (bounded(params.subscriptionId)) observation.subscriptionId = params.subscriptionId;
  if (value.method === 'bridge.bootstrap') {
    requireThat(++bootstrapCount === 1, 'A second bootstrap was attempted.');
  } else {
    requireThat(value.sessionId === sessionId && value.origin === origin, 'Request identity changed.');
    observation.sessionId = sessionId; observation.origin = origin;
  }
  if (value.method === 'task.start') {
    requireThat(++startCount <= 2, 'Business task was replayed.');
    requireThat(params.operationId === (startCount === 1 ? longOperation : resultOperation), 'Task operation identity changed.');
    requireThat(params.kind === (startCount === 1 ? 'probe.long' : 'probe.result'), 'Task kind changed.');
    observation.kind = params.kind;
  }
  if (value.method === 'task.subscribe') {
    requireThat(++subscribeCount === 1 && params.taskId === longTaskId && params.eventName === 'task.progress', 'Subscription was replaced.');
  }
  if (value.method === 'task.unsubscribe') {
    requireThat(++unsubscribeCount === 1 && params.subscriptionId === subscriptionId, 'Original subscription identity was not removed exactly once.');
  }
  return observation;
}
function responseObservation(raw: unknown, request: Observation) {
  requireThat(typeof raw === 'string', 'Native response is not serialized JSON.');
  const response: unknown = JSON.parse(raw);
  requireThat(record(response) && response.requestId === request.requestId && typeof response.ok === 'boolean', 'Native response correlation changed.');
  const observation: Observation = { method: request.method, requestId: request.requestId, ok: response.ok };
  if (!response.ok) {
    requireThat(record(response.error) && bounded(response.error.code), 'Native error lacks a bounded code.');
    observation.errorCode = response.error.code;
  } else {
    const result = record(response.result) ? response.result : {};
    if (request.method === 'bridge.bootstrap' || request.method === 'bridge.session.renew') {
      requireThat(bounded(result.sessionId) && bounded(result.origin) && record(result.lease), 'Renewable native identity is absent.');
      const generation = result.lease.generation;
      requireThat(typeof generation === 'number' && Number.isSafeInteger(generation) && generation >= 1, 'Invalid observed generation.');
      if (request.method === 'bridge.bootstrap') {
        const remaining = result.lease.expiresInMs;
        requireThat(!sessionId && generation === 1 && typeof remaining === 'number' && Number.isSafeInteger(remaining) &&
          remaining > 0 && remaining <= 1800000, 'Unexpected initial native lifetime.');
        sessionId = result.sessionId; origin = result.origin;
        // Receipt + remaining time is an upper bound used only to require later
        // evidence. It never changes the SDK's own conservative lease estimate.
        originalExpiryUpperBound = performance.now() + remaining + 1;
        initialGeneration = generation;
      } else {
        requireThat(result.sessionId === sessionId && result.origin === origin, 'Renewal changed session or origin.');
        requireThat(generation === observedGeneration + 1 ||
          (generation === observedGeneration && request.operationId === lastRenewOperation), 'Observed renewal generation regressed or skipped.');
        lastRenewOperation = request.operationId || '';
      }
      observedGeneration = generation;
      observation.generation = generation; observation.sessionId = result.sessionId; observation.origin = result.origin;
    }
    if (bounded(result.taskId)) observation.taskId = result.taskId;
    if (request.method === 'task.start') {
      requireThat(typeof result.accepted === 'boolean' && typeof result.duplicate === 'boolean', 'Task start ACK fields are missing.');
      observation.accepted = result.accepted; observation.duplicate = result.duplicate;
    }
    if (request.method === 'task.query') {
      requireThat(bounded(result.state) && typeof result.sequence === 'number' && Number.isSafeInteger(result.sequence), 'Task query evidence is incomplete.');
      observation.state = result.state; observation.sequence = result.sequence;
      if (record(result.result) && result.result.value === 42) observation.resultValue = 42;
    }
    if (request.method === 'task.cancel') {
      requireThat(typeof result.cancelRequested === 'boolean' && bounded(result.state), 'Cancel ACK fields are missing.');
      observation.cancelRequested = result.cancelRequested; observation.state = result.state;
    }
    if (request.method === 'task.subscribe') {
      requireThat(bounded(result.subscriptionId) && !subscriptionId, 'Subscription ACK was missing or replaced.');
      subscriptionId = result.subscriptionId; observation.subscriptionId = subscriptionId;
    }
    if (request.method === 'task.unsubscribe') {
      requireThat(request.subscriptionId === subscriptionId && result.removed === true, 'Unsubscribe was not acknowledged.');
      unsubscribeAcknowledged = true;
      observation.subscriptionId = request.subscriptionId; observation.removed = result.removed;
    }
  }
  enqueue(observation);
}
function installObserver() {
  const original = target.__souiNativePostMessage;
  requireThat(typeof original === 'function', 'Native binding is missing.');
  target.__souiNativePostMessage = function (raw: string) {
    let observed: Observation | undefined;
    // Probe persistence is excluded to prevent recursive observation records.
    try {
      const parsed: unknown = JSON.parse(raw);
      if (record(parsed) && parsed.method !== 'probe.record' && parsed.method !== 'probe.fail') {
        observed = requestObservation(raw); enqueue(observed);
      }
    } catch { void fail('Request observation failed.'); }
    // No async boundary before the call; return its exact original object.
    const result = original.call(this, raw);
    if (observed) {
      const snapshot = observed;
      const observe = (response: unknown) => {
        try { responseObservation(response, snapshot); }
        catch { void fail('Response observation failed.'); }
      };
      if (typeof result === 'string') observe(result);
      else void result.then(observe, () => {
        if (snapshot.method === 'bridge.session.renew') {
          // The original rejected Promise remains the SDK's result. Record
          // only the transport outcome; its bounded same-operation recovery
          // must run before the SDK's final onError can fail this fixture.
          try {
            enqueue({ method: snapshot.method, requestId: snapshot.requestId,
              operationId: snapshot.operationId, transportError: true, errorCode: 'E_TRANSPORT' });
          } catch { void fail('Transport observation failed.'); }
        } else void fail('Native transport rejected a request.');
      });
    }
    return result;
  };
}
async function flushProtocol() {
  healthy();
  while (protocolQueue.length) {
    const observations: Observation[] = [];
    while (protocolQueue.length && observations.length < 12) {
      if (JSON.stringify([...observations, protocolQueue[0]]).length > 4000) break;
      observations.push(protocolQueue.shift()!);
    }
    requireThat(observations.length > 0, 'Protocol batch exceeds the bounded record size.');
    await client!.call('probe.record', { kind: 'protocol', observations }); healthy();
  }
}
function currentLease(): SouiSessionLease {
  const lease = client!.sessionLease;
  requireThat(lease && lease.expiresInMs > 0 && lease.generation >= lastLeaseGeneration && lease.generation <= observedGeneration, 'Client lease is absent, expired or regressed.');
  lastLeaseGeneration = lease.generation;
  return lease;
}
function counts() {
  requireThat(bootstrapCount === 1 && startCount === 2 && subscribeCount === 1, 'A bootstrap, task or subscription was recreated.');
  return { bootstrapCount, startCount, subscribeCount };
}
function fixedIdentity() { return { documentNonce, sessionId, longTaskId, resultTaskId, subscriptionId }; }
function show(message: string) {
  status.textContent = message;
  status.className = phase === 'failed' ? 'failed' : phase === 'done' ? 'complete' : '';
  details.textContent = JSON.stringify({ phase, runId: description?.runId, durationMs: description?.durationMs,
    elapsedMs: startedAt ? elapsed() : 0, ...fixedIdentity(), lease: client?.sessionLease,
    eventCount, lastEventSequence, querySequence, bootstrapCount, startCount, subscribeCount, unsubscribeCount,
    postOriginalExpiryEvent, postOriginalExpiryHeartbeat, failure }, null, 2);
}
async function fail(message: string) {
  if (failure || phase === 'done') return;
  failure = message.slice(0, 240); phase = 'failed'; show('本轮失败，等待原生收尾');
  if (!client) return; // Native ready watchdog handles failure before bootstrap.
  try { await client.call('probe.record', { kind: 'failure', ...fixedIdentity(), message: failure, bootstrapCount, startCount, subscribeCount, eventCount }); }
  catch { /* Never turn logger or expired-auth failure into success. Native watchdog remains active. */ }
  try { await client.call('probe.fail', { message: failure }); }
  catch { /* No reconnect or new bootstrap. */ }
  client.dispose();
}
function onProgress(event: SouiNativeEvent) {
  if (phase === 'failed' || phase === 'done') return;
  try {
    requireThat(event.sessionId === sessionId && event.taskId === longTaskId && event.eventName === 'task.progress', 'Progress identity changed.');
    requireThat(event.sequence > lastEventSequence, 'Progress sequence did not advance.');
    lastEventSequence = event.sequence; lastEventAt = performance.now(); ++eventCount;
    if (lastEventAt > originalExpiryUpperBound) postOriginalExpiryEvent = true;
  } catch { void fail('Progress event validation failed.'); }
}
async function queryResult() {
  const result = await client!.call('task.query', { taskId: resultTaskId }); healthy();
  requireThat(result.taskId === resultTaskId && result.state === 'succeeded' && result.result?.value === 42, 'Original completed task result was not retained.');
  return result;
}
async function heartbeat() {
  healthy(); requireThat(subscription === initialHandle && !subscription!.closed, 'Original subscribed handle is no longer active.');
  const long = await client!.call('task.query', { taskId: longTaskId }); healthy();
  requireThat(long.taskId === longTaskId && long.state === 'running', 'Original long task is not running.');
  requireThat(Number.isSafeInteger(long.sequence) && long.sequence >= querySequence, 'Query sequence regressed.'); querySequence = long.sequence;
  await queryResult();
  requireThat(eventCount > priorHeartbeatEventCount && performance.now() - lastEventAt <= 15000, 'Heartbeat has no continuing progress events.');
  priorHeartbeatEventCount = eventCount;
  const lease = currentLease();
  if (performance.now() > originalExpiryUpperBound && postOriginalExpiryEvent) postOriginalExpiryHeartbeat = true;
  await flushProtocol();
  const sampledElapsed = elapsed();
  await client!.call('probe.record', { kind: 'heartbeat', ...fixedIdentity(), ...counts(), elapsedMs: sampledElapsed, lease,
    eventCount, lastEventSequence, lastEventElapsedMs: Math.floor(lastEventAt - startedAt), querySequence,
    postOriginalExpiryEvent, postOriginalExpiryHeartbeat }); healthy(); lastRecordedElapsed = sampledElapsed; show('同一任务与订阅持续运行');
}
async function finish() {
  if (lastRecordedElapsed < description!.durationMs) {
    // The final duration remainder can be shorter than the native 1s event
    // cadence. Wait for actual progress within the existing silence deadline,
    // rather than treating two closely spaced samples as a stalled task.
    while (eventCount <= priorHeartbeatEventCount) {
      healthy();
      const remaining = 15000 - (performance.now() - lastEventAt);
      requireThat(remaining > 0, 'Final heartbeat has no continuing progress events.');
      await pause(Math.min(100, remaining));
    }
    await heartbeat();
  }
  phase = 'finishing';
  const lease = currentLease(), longRun = description!.durationMs >= 2100000;
  requireThat(!longRun || (lease.generation >= 2 && postOriginalExpiryEvent && postOriginalExpiryHeartbeat), 'No post-original-expiry renewal continuity evidence.');
  requireThat(description!.durationMs !== 28800000 || lease.generation >= 20, 'Eight-hour run lacks the expected renewal generations.');
  const cancellation = await client!.call('task.cancel', { taskId: longTaskId }); healthy();
  requireThat(cancellation.taskId === longTaskId && cancellation.cancelRequested === true, 'Original task cancellation request was not acknowledged.');
  const deadline = performance.now() + 15000;
  let cancelled = false;
  while (performance.now() < deadline) {
    const state = await client!.call('task.query', { taskId: longTaskId }); healthy();
    requireThat(state.taskId === longTaskId, 'Cancelled task identity changed.');
    if (state.state === 'cancelled') { cancelled = true; break; }
    requireThat(state.state === 'running' || state.state === 'cancel-pending' || state.state === 'queued', 'Long task ended without confirmed cancellation.');
    await flushProtocol();
    await pause(100);
  }
  requireThat(cancelled, 'Cancellation acknowledgement deadline exceeded.');
  requireThat(subscription === initialHandle, 'Subscription handle changed before cleanup.');
  await subscription!.unsubscribe(); healthy();
  requireThat(unsubscribeAcknowledged && unsubscribeCount === 1 && subscription!.closed, 'Original unsubscribe ACK is missing.');
  await queryResult(); await flushProtocol();
  await client!.call('probe.record', { kind: 'complete', ...fixedIdentity(), ...counts(), elapsedMs: elapsed(), lease: currentLease(),
    eventCount, lastEventSequence, querySequence, unsubscribeCount, cancelled: true, unsubscribed: true, resultValue: 42,
    postOriginalExpiryEvent, postOriginalExpiryHeartbeat, realRenewalVerified: longRun }); healthy();
  phase = 'awaiting-finish'; show('已记录完成条件，等待原生最终收尾');
  finishSent = true;
  try { await client!.call('probe.finish', {}); healthy(); phase = 'done'; show('完成条件已确认；最终结果以原生日志和退出码为准'); }
  catch (error) {
    if (!failure && failureCode(error) === 'E_DISPOSED') show('完成条件已记录，最终 finish ACK 未观察到；请核对原生日志和退出码');
    else { finishSent = false; await fail(`Finish request failed (${failureCode(error)}).`); }
  }
}
async function run() {
  installObserver();
  // Exactly one standard connection, default native lease and SDK timers.
  client = await connectSouiNative<Methods>({ onError: error => {
    if (!(finishSent && failureCode(error) === 'E_DISPOSED')) void fail(`SDK client reported ${failureCode(error)}.`);
  } }); healthy();
  phase = 'preparing';
  description = await client.call('probe.describe', {}); healthy();
  requireThat(bounded(description.runId) && [30000, 2100000, 28800000].indexOf(description.durationMs) !== -1 &&
    description.version === '0.3.0-preview.11' && ['x86', 'x64'].indexOf(description.architecture) !== -1, 'Invalid frozen probe description.');
  requireThat(description.runId.length <= 100, 'Run identity exceeds task operation bounds.');
  longOperation = `${description.runId}:long`; resultOperation = `${description.runId}:result`;
  const long = await client.call('task.start', { kind: 'probe.long', operationId: longOperation, input: {} }); healthy();
  requireThat(long.accepted && !long.duplicate && bounded(long.taskId), 'Long task was not newly admitted.'); longTaskId = long.taskId;
  const result = await client.call('task.start', { kind: 'probe.result', operationId: resultOperation, input: {} }); healthy();
  requireThat(result.accepted && !result.duplicate && bounded(result.taskId) && result.taskId !== longTaskId, 'Result task was not newly admitted.'); resultTaskId = result.taskId;
  subscription = client.subscribe(longTaskId, 'task.progress', onProgress); initialHandle = subscription;
  await subscription.ready; healthy(); requireThat(bounded(subscriptionId), 'Original native subscription ACK was not observed.');
  const deadline = performance.now() + 15000;
  let prepared = false;
  while (performance.now() < deadline) {
    const state = await client.call('task.query', { taskId: resultTaskId }); healthy();
    requireThat(state.taskId === resultTaskId && ['queued', 'running', 'succeeded'].indexOf(state.state) !== -1, 'Result task preparation failed.');
    if (state.state === 'succeeded' && state.result?.value === 42 && eventCount > 0) { prepared = true; break; }
    await flushProtocol();
    await pause(100);
  }
  requireThat(prepared, 'Tasks did not become observable within the preparation deadline.');
  currentLease(); counts(); await flushProtocol();
  await client.call('probe.record', { kind: 'begin', ...fixedIdentity(), initialGeneration, durationMs: description.durationMs }); healthy();
  await client.call('bridge.probe.report', { ready: true }); healthy();
  await client.call('probe.ready', {}); healthy(); startedAt = performance.now(); phase = 'running';
  // A first real progress event was required above; each subsequent 5s sample
  // must receive additional events, regardless of successful query polling.
  priorHeartbeatEventCount = eventCount; show('准备完成，开始真实时钟观察');
  while (elapsed() < description.durationMs) {
    await pause(Math.min(5000, description.durationMs - elapsed())); healthy();
    if (elapsed() < description.durationMs) await heartbeat();
  }
  await finish();
}
function showInteraction() {
  interaction.textContent = `输入事件 ${inputEvents} · 组合事件 ${compositionEvents} · 字符 ${typing.value.length} · 窗口 ${window.innerWidth} × ${window.innerHeight} · resize ${resizeEvents}`;
}
typing.addEventListener('input', () => { ++inputEvents; showInteraction(); });
for (const event of ['compositionstart', 'compositionupdate', 'compositionend']) typing.addEventListener(event, () => { ++compositionEvents; showInteraction(); });
window.addEventListener('resize', () => { ++resizeEvents; showInteraction(); });
window.addEventListener('beforeunload', () => { if (!finishSent) void fail('Document exited before completion.'); });
window.addEventListener('error', () => { void fail('Unhandled page error.'); });
window.addEventListener('unhandledrejection', () => { void fail('Unhandled page promise rejection.'); });
showInteraction();
void run().catch(error => { void fail(error instanceof ProbeFailure ? error.message : `Probe check failed (${failureCode(error)}).`); });
