import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  MAX_ASSET_BYTES, DesktopAdapterError, requireDesktop, invoke, on, readAsset,
  assetBytes, assetText, assetJson, windowControls, windowProperties, clipboard, dialogs
} from '../../sdk/desktop/frontend/desktop.mjs';

const asset = (value) => {
  const bytes = Buffer.from(value);
  return { mime: 'application/octet-stream', size: bytes.length, base64: bytes.toString('base64') };
};
const adapterError = (code) => (error) =>
  error instanceof DesktopAdapterError && error.code === `desktop_adapter_${code}`;

test('desktop service facades use reserved methods and preserve native failures', async () => {
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'window');
  try {
    const calls = [];
    let response = { accepted: true };
    const host = {
      version: 1, on() {}, readAsset() {},
      invoke(method, params) {
        assert.equal(this, host);
        calls.push([method, params]);
        return Promise.resolve(response);
      }
    };
    globalThis.window = { souiDesktop: host };
    for (const facade of [windowProperties, clipboard, dialogs]) assert.ok(Object.isFrozen(facade));
    assert.deepEqual(await windowProperties.setTitle('中文'), { accepted: true });
    assert.deepEqual(await windowProperties.resize(640, 480), { accepted: true });
    assert.deepEqual(await windowProperties.show(false), { accepted: true });
    assert.deepEqual(await windowProperties.focus(), { accepted: true });
    assert.deepEqual(await clipboard.writeText('hello'), { accepted: true });
    response = { text: '' };
    assert.equal(await clipboard.readText(), '');
    response = { cancelled: true, path: '' };
    assert.deepEqual(await dialogs.chooseDirectory(), response);
    response = { cancelled: false, path: '/中文/🦊' };
    assert.deepEqual(await dialogs.chooseDirectory(), response);
    assert.deepEqual(calls, [
      ['desktop.window.setTitle', { title: '中文' }],
      ['desktop.window.resize', { width: 640, height: 480 }],
      ['desktop.window.show', { visible: false }], ['desktop.window.focus', {}],
      ['desktop.clipboard.writeText', { text: 'hello' }], ['desktop.clipboard.readText', {}],
      ['desktop.dialog.chooseDirectory', {}], ['desktop.dialog.chooseDirectory', {}]
    ]);
    const operations = [() => windowProperties.setTitle(''), () => windowProperties.resize(1, 2),
      () => windowProperties.show(true), () => windowProperties.focus(),
      () => clipboard.writeText(''), () => clipboard.readText(), () => dialogs.chooseDirectory()];
    const nativeError = Object.assign(new Error('denied'), { code: 'E_PERMISSION' });
    host.invoke = () => Promise.reject(nativeError);
    for (const operation of operations) await assert.rejects(operation(), error => error === nativeError);
    delete globalThis.window;
    for (const operation of operations) assert.throws(operation, adapterError('missing_host'));
  } finally {
    if (previous) Object.defineProperty(globalThis, 'window', previous);
    else delete globalThis.window;
  }
});

test('desktop service results reject false defaults and enforce strict bounded UTF-8', async () => {
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'window');
  try {
    let response;
    globalThis.window = { souiDesktop: {
      version: 1, on() {}, readAsset() {}, invoke() { return Promise.resolve(response); }
    } };
    for (response of [null, [], {}, { accepted: false }, { accepted: 1 }]) {
      await assert.rejects(windowProperties.focus(), adapterError('invalid_service_acceptance'));
      await assert.rejects(clipboard.writeText(''), adapterError('invalid_service_acceptance'));
    }
    for (response of [null, [], {}, { text: null }, { text: 'a\0b' }, { text: '\ud800' },
      { text: '\udc00' }, { text: 'a'.repeat(65537) }, { text: '🦊'.repeat(16385) }]) {
      await assert.rejects(clipboard.readText(), adapterError('invalid_clipboard_text'));
    }
    for (const text of ['a'.repeat(65536), '🦊'.repeat(16384), '中文']) {
      response = { text };
      assert.equal(await clipboard.readText(), text);
      response = { cancelled: false, path: text };
      assert.deepEqual(await dialogs.chooseDirectory(), response);
    }
    for (response of [null, [], {}, { cancelled: false, path: '' }, { cancelled: true, path: '/x' },
      { cancelled: 0, path: '/x' }, { cancelled: false, path: '/\0' },
      { cancelled: false, path: '\ud800' }, { cancelled: false, path: '中'.repeat(21846) }]) {
      await assert.rejects(dialogs.chooseDirectory(), adapterError('invalid_directory_selection'));
    }
    let reads = 0;
    response = { get text() { return ++reads === 1 ? 'valid' : '\0'; } };
    assert.equal(await clipboard.readText(), 'valid');
    assert.equal(reads, 1);
    let cancelledReads = 0, pathReads = 0;
    response = {
      get cancelled() { ++cancelledReads; return false; },
      get path() { return ++pathReads === 1 ? '/valid' : ''; }
    };
    assert.deepEqual(await dialogs.chooseDirectory(), { cancelled: false, path: '/valid' });
    assert.equal(cancelledReads, 1);
    assert.equal(pathReads, 1);
  } finally {
    if (previous) Object.defineProperty(globalThis, 'window', previous);
    else delete globalThis.window;
  }
});

test('host checks are immediate, and calls preserve native identity and receiver', async () => {
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'window');
  try {
    delete globalThis.window;
    assert.throws(requireDesktop, adapterError('missing_host'));
    assert.throws(() => invoke('ping'), adapterError('missing_host'));
    globalThis.window = { souiDesktop: { version: 2 } };
    assert.throws(requireDesktop, adapterError('incompatible_host'));
    globalThis.window.souiDesktop = { version: 1, invoke() {}, on() {} };
    assert.throws(requireDesktop, adapterError('incompatible_host'));
    const nativeError = Object.assign(new Error('Native request failed'), { code: 'denied' });
    const result = Promise.resolve({ ok: true });
    const rejected = Promise.reject(nativeError);
    const params = { nested: [1] };
    const listener = () => {};
    const unsubscribe = () => {};
    const host = {
      version: 1,
      invoke(method, value) {
        assert.equal(this, host);
        assert.equal(method, 'app.ping');
        assert.equal(value, params);
        return result;
      },
      on(event, callback) {
        assert.equal(this, host);
        assert.equal(event, 'app.changed');
        assert.equal(callback, listener);
        return unsubscribe;
      },
      readAsset(path) {
        assert.equal(this, host);
        assert.equal(path, 'data.json');
        return rejected;
      }
    };
    globalThis.window.souiDesktop = host;
    assert.equal(requireDesktop(), host);
    assert.equal(invoke('app.ping', params), result);
    assert.equal(on('app.changed', listener), unsubscribe);
    assert.equal(readAsset('data.json'), rejected);
    await assert.rejects(rejected, (error) => error === nativeError);
    host.invoke = () => { throw nativeError; };
    assert.throws(() => invoke('app.ping'), (error) => error === nativeError);
  } finally {
    if (previous) Object.defineProperty(globalThis, 'window', previous);
    else delete globalThis.window;
  }
});

test('window controls delegate reserved methods and observe state independently of acceptance', async () => {
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'window');
  try {
    delete globalThis.window;
    for (const method of ['minimize', 'maximize', 'restore', 'getState']) {
      assert.throws(() => windowControls[method](), adapterError('missing_host'));
    }
    assert.throws(() => windowControls.onStateChanged(() => {}), adapterError('missing_host'));
    const calls = [];
    const accepted = Promise.resolve({ accepted: true });
    const observed = { minimized: true, maximized: true };
    let callback;
    let unsubscriptions = 0;
    const unsubscribe = () => { ++unsubscriptions; };
    const host = {
      version: 1,
      invoke(method, params) {
        assert.equal(this, host);
        assert.deepEqual(params, {});
        calls.push(method);
        return method === 'desktop.window.getState' ? Promise.resolve(observed) : accepted;
      },
      on(event, listener) {
        assert.equal(this, host);
        assert.equal(event, 'desktop.window.stateChanged');
        callback = listener;
        return unsubscribe;
      },
      readAsset() {}
    };
    globalThis.window = { souiDesktop: host };
    const events = [];
    assert.equal(windowControls.onStateChanged(value => events.push(value)), unsubscribe);
    for (const method of ['minimize', 'maximize', 'restore']) {
      assert.equal(windowControls[method](), accepted);
    }
    assert.deepEqual(events, []); // Acceptance must not synthesize an observation.
    const state = await windowControls.getState();
    assert.deepEqual(state, observed);
    assert.notEqual(state, observed);
    callback(observed);
    assert.deepEqual(events, [observed]);
    assert.notEqual(events[0], observed);
    assert.deepEqual(calls, ['desktop.window.minimize', 'desktop.window.maximize',
      'desktop.window.restore', 'desktop.window.getState']);
    unsubscribe();
    assert.equal(unsubscriptions, 1);
    const nativeError = Object.assign(new Error('No native observation'), { code: 'E_STATE' });
    globalThis.window.souiDesktop = {
      ...host, invoke() { return Promise.reject(nativeError); }
    };
    for (const method of ['minimize', 'maximize', 'restore', 'getState']) {
      await assert.rejects(windowControls[method](), error => error === nativeError);
    }
  } finally {
    if (previous) Object.defineProperty(globalThis, 'window', previous);
    else delete globalThis.window;
  }
});

test('window state snapshots validate booleans once and reject missing or malformed observations', async () => {
  const previous = Object.getOwnPropertyDescriptor(globalThis, 'window');
  try {
    let response;
    let callback;
    const states = [];
    globalThis.window = { souiDesktop: {
      version: 1,
      invoke() { return Promise.resolve(response); },
      on(event, listener) { callback = listener; return () => {}; },
      readAsset() {}
    } };
    windowControls.onStateChanged(state => states.push(state));
    for (const invalid of [undefined, null, {}, [], { minimized: false },
      { minimized: 0, maximized: false }, { minimized: false, maximized: 'false' }]) {
      response = invalid;
      await assert.rejects(windowControls.getState(), adapterError('invalid_window_state'));
      assert.throws(() => callback(invalid), adapterError('invalid_window_state'));
    }
    assert.deepEqual(states, []);
    assert.throws(() => windowControls.onStateChanged(null), TypeError);
    for (const minimized of [false, true]) for (const maximized of [false, true]) {
      response = { minimized, maximized };
      assert.deepEqual(await windowControls.getState(), response);
    }
    let minimizedReads = 0, maximizedReads = 0;
    response = {
      get minimized() { return ++minimizedReads === 1 ? false : 'changed'; },
      get maximized() { return ++maximizedReads === 1 ? true : 'changed'; }
    };
    assert.deepEqual(await windowControls.getState(), { minimized: false, maximized: true });
    assert.equal(minimizedReads, 1);
    assert.equal(maximizedReads, 1);
  } finally {
    if (previous) Object.defineProperty(globalThis, 'window', previous);
    else delete globalThis.window;
  }
});

test('asset decoding accepts canonical payloads through the exact size cap', () => {
  for (const length of [0, 1, 2, 3, 4, MAX_ASSET_BYTES]) {
    const bytes = Buffer.alloc(length, 197);
    assert.deepEqual(assetBytes(asset(bytes)), new Uint8Array(bytes));
  }
  assert.equal(assetText(asset('中文 🦊')), '中文 🦊');
  assert.deepEqual(assetJson(asset('{"ready":true}')), { ready: true });
});

test('asset decoding snapshots accessor values before validation and allocation', () => {
  let sizeReads = 0, base64Reads = 0;
  const response = {
    mime: 'text/plain',
    get size() {
      // This coerces to a small number but allocates an oversized typed array
      // if validation reads a different value from the allocation step.
      return ++sizeReads <= 2 ? 1 : { length: MAX_ASSET_BYTES + 1, valueOf: () => 1 };
    },
    get base64() { ++base64Reads; return 'YQ=='; }
  };
  assert.deepEqual(assetBytes(response), new Uint8Array([97]));
  assert.equal(sizeReads, 1);
  assert.equal(base64Reads, 1);
});

test('asset decoding rejects malformed size, encoding, UTF-8 and JSON locally', () => {
  for (const invalid of [
    null, {}, { ...asset('a'), mime: 1 }, { ...asset('a'), size: -1 },
    { ...asset('a'), size: 1.5 }, { ...asset('a'), size: 2 },
    { ...asset('a'), base64: 'YR==' }, { ...asset('ab'), base64: 'YWJ=' },
    { ...asset('a'), base64: 'YQ  ' }, { ...asset('a'), base64: 'YQ--' },
    { ...asset('a'), base64: 'YQ==\n' }, { ...asset('a'), base64: 'YQ' }
  ]) assert.throws(() => assetBytes(invalid), adapterError('invalid_asset'));
  assert.throws(() => assetBytes({ mime: '', size: MAX_ASSET_BYTES + 1, base64: '' }),
    adapterError('asset_too_large'));
  assert.throws(() => assetText(asset([0xc3, 0x28])), adapterError('invalid_utf8'));
  assert.throws(() => assetJson(asset('{')), adapterError('invalid_json'));
});
