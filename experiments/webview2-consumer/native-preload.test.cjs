'use strict';
const test = require('node:test');
const assert = require('node:assert/strict');
const fs = require('node:fs');
const path = require('node:path');
const vm = require('node:vm');
const source = fs.readFileSync(path.join(__dirname, 'native-preload.js'), 'utf8');

function fixture({ url = 'https://soui-webview2.invalid/index.html', frame = false, throws = false } = {}) {
  const timers = new Map();
  const events = new Map();
  const wireEvents = new Map();
  const sent = [];
  let sequence = 0;
  const window = {
    location: { href: url },
    addEventListener: (name, fn) => events.set(name, fn),
    removeEventListener: (name, fn) => { if (events.get(name) === fn) events.delete(name); },
    chrome: { webview: {
      addEventListener: (name, fn) => wireEvents.set(name, fn),
      removeEventListener: (name, fn) => { if (wireEvents.get(name) === fn) wireEvents.delete(name); },
      postMessage: raw => { if (throws) throw new Error('post failed'); sent.push(JSON.parse(raw)); },
    } },
  };
  window.top = frame ? {} : window;
  vm.runInNewContext(source.replace('__SOUI_DOCUMENT_ID__', JSON.stringify('document-current')), {
    window, setTimeout: callback => { timers.set(++sequence, callback); return sequence; },
    clearTimeout: id => timers.delete(id),
  });
  return { window, timers, events, wireEvents, sent,
    request: (id, more = {}) => window.__souiNativePostMessage(JSON.stringify({ requestId: id, ...more })),
    response: (id, documentId = 'document-current') => wireEvents.get('message')?.({
      data: { kind: 'response', documentId, requestId: id, raw: JSON.stringify({ requestId: id, ok: true, result: {} }) },
    }),
  };
}

test('preload only exposes the binding to the exact trusted top-level document', () => {
  for (const options of [{ frame: true }, { url: 'https://soui-webview2.invalid.evil/index.html' },
    { url: 'https://soui-webview2.invalid/other.html' }, { url: 'https://soui-webview2.invalid/index.html?other' }]) {
    assert.equal(fixture(options).window.__souiNativePostMessage, undefined);
  }
  assert.equal(typeof fixture({ url: 'https://soui-webview2.invalid/index.html#view' }).window.__souiNativePostMessage, 'function');
});

test('old document responses cannot resolve a reused request ID; completion releases the timer', async () => {
  const f = fixture();
  const result = f.request('same-id');
  f.response('same-id', 'document-old');
  assert.equal(f.timers.size, 1);
  assert.equal(f.sent[0].documentId, 'document-current');
  f.response('same-id');
  assert.equal(JSON.parse(await result).requestId, 'same-id');
  assert.equal(f.timers.size, 0);
});

test('duplicate IDs and capacity overload reject without overwriting in-flight work', async () => {
  const f = fixture();
  const pending = Array.from({ length: 128 }, (_, i) => f.request(String(i)));
  await assert.rejects(f.request('0'), /duplicate/);
  await assert.rejects(f.request('overflow'), /capacity/);
  assert.equal(f.sent.length, 128);
  for (let i = 0; i < 128; i++) f.response(String(i));
  await Promise.all(pending);
  assert.equal(f.timers.size, 0);
  const recovered = f.request('after');
  f.response('after');
  await recovered;
});

test('unload rejects pending work and detaches native dispatch exactly once', async () => {
  const f = fixture();
  const pending = f.request('pending');
  const rejected = assert.rejects(pending, /closed/);
  f.events.get('pagehide')();
  f.events.get('beforeunload')();
  await rejected;
  assert.equal(f.timers.size, 0);
  assert.equal(f.wireEvents.size, 0);
  await assert.rejects(f.request('after-close'), /closed/);
});

test('timeout and postMessage failure release capacity and do not accept late results', async () => {
  const f = fixture();
  const pending = f.request('late', { timeoutMs: 1 });
  const rejected = assert.rejects(pending, /timed out/);
  const callback = [...f.timers.values()][0];
  f.timers.clear();
  callback();
  await rejected;
  f.response('late');
  const broken = fixture({ throws: true });
  await assert.rejects(broken.request('broken'), /post failed/);
  assert.equal(broken.timers.size, 0);
});

test('events are delivered only for the current document generation', () => {
  const f = fixture();
  const seen = [];
  f.window.__souiNativeDispatchEvent = event => seen.push(event);
  for (const documentId of ['document-old', 'document-current']) {
    f.wireEvents.get('message')({ data: { kind: 'event', documentId, data: { eventName: 'task.progress' } } });
  }
  assert.equal(seen.length, 1);
});
