import test from 'node:test';
import assert from 'node:assert/strict';
import fs from 'node:fs';
import vm from 'node:vm';

const source = fs.readFileSync(new URL('../../sdk/renderers/webview2/native-preload.js', import.meta.url), 'utf8');
function fixture(t, documentId = 'document:A') {
  const timers = new Map(), listeners = new Map(), messages = new Set(), posted = [];
  let nextTimer = 1, now = 0, postError;
  const window = {
    location: { href: 'https://soui-webview2.invalid/index.html' },
    addEventListener(type, callback) {
      const callbacks = listeners.get(type) || new Set(); callbacks.add(callback); listeners.set(type, callbacks);
    },
    chrome: { webview: {
      addEventListener(type, callback) { assert.equal(type, 'message'); messages.add(callback); },
      removeEventListener(type, callback) { assert.equal(type, 'message'); messages.delete(callback); },
      postMessage(raw) { if (postError) throw postError; posted.push(JSON.parse(raw)); },
    } },
  };
  window.top = window;
  const close = type => { for (const callback of listeners.get(type || 'pagehide') || []) callback(); };
  vm.runInNewContext(source.replace('__SOUI_DOCUMENT_ID__', JSON.stringify(documentId)), {
    window, TextEncoder,
    setTimeout(callback, delay) { const id = nextTimer++; timers.set(id, { callback, at: now + delay }); return id; },
    clearTimeout(id) { timers.delete(id); },
  });
  const send = (id, method = 'app.work', params = {}, extra = {}) => {
    const promise = Promise.resolve(window.__souiNativePostMessage(JSON.stringify({
      requestId: id, sessionId: 'session:A', origin: 'app://test', token: 'a'.repeat(64),
      method, params, timeoutMs: 10000, ...extra,
    })));
    promise.catch(() => {}); // Closing a fixture rejects its intentionally unfinished requests.
    return promise;
  };
  t.after(() => { close(); assert.equal(timers.size, 0); assert.equal(messages.size, 0); });
  return {
    send, close, timers, posted, window,
    setPostError(error) { postError = error; },
    complete(id, raw = '{}', identity = documentId) {
      for (const callback of messages) callback({ data: { kind: 'response', documentId: identity, requestId: id, raw } });
    },
    advance(delay) {
      now += delay;
      for (const [id, timer] of [...timers]) if (timer.at <= now && timers.delete(id)) timer.callback();
    },
  };
}
function saturate(f) { return Array.from({ length: 128 }, (_, id) => f.send(`work:${id}`)); }
const renew = id => ({ operationId: id });

test('128 business requests leave independent bounded renewal and cleanup capacity', async t => {
  const f = fixture(t), work = saturate(f);
  const renewal = f.send('renew:1', 'bridge.session.renew', renew('rotation:1'));
  const cleanup = Array.from({ length: 128 }, (_, id) => f.send(`cleanup:${id}`,
    id % 2 ? 'task.cancel' : 'task.unsubscribe', id % 2 ? { taskId: `task:${id}` } : { subscriptionId: `sub:${id}` }));
  assert.equal(f.posted.length, 257);
  await assert.rejects(f.send('work:overflow'), /capacity/);
  await assert.rejects(f.send('renew:overflow', 'bridge.session.renew', renew('rotation:2')), /capacity/);
  await assert.rejects(f.send('cleanup:overflow', 'task.cancel', { taskId: 'task:overflow' }), /capacity/);
  await assert.rejects(f.send('work:0', 'bridge.session.renew', renew('same-id')), /duplicate/);
  await assert.rejects(f.send('renew:1'), /duplicate/);
  f.complete('renew:1', 'rotated'); assert.equal(await renewal, 'rotated');
  const next = f.send('renew:2', 'bridge.session.renew', renew('rotation:2'));
  f.complete('renew:2', 'again'); assert.equal(await next, 'again');
  f.complete('cleanup:0', 'removed'); assert.equal(await cleanup[0], 'removed');
  await assert.rejects(f.send('work:still-full'), /capacity/);
  f.close(); await Promise.allSettled([...work, ...cleanup]);
});

test('claimed priority and malformed control envelopes cannot consume reserved capacity', async t => {
  const f = fixture(t); saturate(f);
  for (const [method, params, extra] of [
    ['app.work', {}, { control: true, renewal: true, priority: 'renewal' }],
    ['bridge.session.renew', {}, {}],
    ['bridge.session.renew', { operationId: 'ok', unexpected: true }, {}],
    ['bridge.session.renew', renew('bad space'), {}],
    ['bridge.session.renew', renew('x'.repeat(129)), {}],
    ['bridge.session.renew', renew('ok'), { token: '' }],
    ['task.cancel', { subscriptionId: 'wrong' }, {}],
    ['task.unsubscribe', { subscriptionId: 'x'.repeat(257) }, {}],
  ]) await assert.rejects(f.send(`invalid:${method}:${f.posted.length}`, method, params, extra), /capacity/);
  const valid = f.send('valid', 'bridge.session.renew', renew('ok'));
  f.complete('valid', 'allowed'); assert.equal(await valid, 'allowed');
});

test('a timeout returns only its lane and does not cancel the native operation', async t => {
  const f = fixture(t); saturate(f);
  const renewal = f.send('renew:timeout', 'bridge.session.renew', renew('timeout'), { timeoutMs: 10 });
  const cleanup = f.send('cleanup:waiting', 'task.cancel', { taskId: 'task:A' });
  f.advance(10);
  await assert.rejects(renewal, /timed out.*not cancelled/);
  assert.equal(f.timers.size, 129);
  const next = f.send('renew:retry', 'bridge.session.renew', renew('timeout'));
  f.complete('renew:timeout', 'late'); assert.equal(f.timers.size, 130);
  f.complete('renew:retry', 'recovered'); assert.equal(await next, 'recovered');
  await assert.rejects(f.send('ordinary:still-full'), /capacity/);
  f.complete('cleanup:waiting', 'cancel-requested'); assert.equal(await cleanup, 'cancel-requested');
});

test('synchronous post failure returns renewal and cleanup slots and removes their timers', async t => {
  const f = fixture(t); saturate(f); f.setPostError(new Error('post failed'));
  await assert.rejects(f.send('renew:failed', 'bridge.session.renew', renew('failed')), /post failed/);
  await assert.rejects(f.send('cleanup:failed', 'task.cancel', { taskId: 'task:A' }), /post failed/);
  assert.equal(f.timers.size, 128);
  f.setPostError(undefined);
  const next = f.send('renew:recovered', 'bridge.session.renew', renew('failed'));
  f.complete('renew:recovered', 'ok'); assert.equal(await next, 'ok');
});

test('business and cleanup timeout return their own slots while renewal remains pending', async t => {
  const f = fixture(t);
  const first = f.send('work:expires', 'app.work', {}, { timeoutMs: 1 });
  for (let id = 1; id < 128; ++id) f.send(`work:${id}`);
  const cleanup = f.send('cleanup:expires', 'task.cancel', { taskId: 'task:A' }, { timeoutMs: 2 });
  for (let id = 1; id < 128; ++id) f.send(`cleanup:${id}`, 'task.cancel', { taskId: `task:${id}` });
  const renewal = f.send('renew:waiting', 'bridge.session.renew', renew('waiting'));
  f.advance(1); await assert.rejects(first, /timed out/);
  f.send('work:replacement');
  await assert.rejects(f.send('work:overflow'), /capacity/);
  f.advance(1); await assert.rejects(cleanup, /timed out/);
  const replacement = f.send('cleanup:replacement', 'task.unsubscribe', { subscriptionId: 'sub:A' });
  await assert.rejects(f.send('cleanup:overflow', 'task.cancel', { taskId: 'task:A' }), /capacity/);
  await assert.rejects(f.send('renew:overflow', 'bridge.session.renew', renew('extra')), /capacity/);
  f.complete('cleanup:replacement', 'removed'); assert.equal(await replacement, 'removed');
  f.complete('renew:waiting', 'rotated'); assert.equal(await renewal, 'rotated');
});

test('invalid timeouts and UTF-8 request identities consume no timers or control capacity', async t => {
  const f = fixture(t);
  for (const timeoutMs of [0, 60001, 1.5])
    await assert.rejects(f.send('renew:invalid', 'bridge.session.renew', renew('valid'), { timeoutMs }), /timeout/);
  await assert.rejects(f.send('界'.repeat(172), 'bridge.session.renew', renew('valid')), /requestId/);
  assert.equal(f.timers.size, 0); assert.equal(f.posted.length, 0);
  const valid = f.send('valid', 'bridge.session.renew', renew('valid'));
  f.complete('valid', 'accepted'); assert.equal(await valid, 'accepted');
});

test('old document responses cannot settle current waits; close rejects every lane and blocks sends', async t => {
  const f = fixture(t, 'document:B');
  const work = f.send('same-id'), renewal = f.send('renew', 'bridge.session.renew', renew('once'));
  const cleanup = f.send('cancel', 'task.cancel', { taskId: 'task:A' });
  f.complete('same-id', 'obsolete', 'document:A'); assert.equal(f.timers.size, 3);
  f.complete('same-id', 'current'); assert.equal(await work, 'current');
  f.close('beforeunload'); f.close();
  await assert.rejects(renewal, /Document closed/); await assert.rejects(cleanup, /Document closed/);
  await assert.rejects(f.send('new', 'bridge.session.renew', renew('new')), /Document closed/);
  assert.equal(f.timers.size, 0); assert.equal(f.posted.length, 3);
});

test('transport enforces UTF-8 bytes rather than only UTF-16 length', async t => {
  const f = fixture(t);
  const request = f.send('unicode', 'app.work', { value: '界'.repeat(22000) });
  assert.equal(f.posted.length, 0);
  await assert.rejects(request, /limit/); assert.equal(f.timers.size, 0);
});
