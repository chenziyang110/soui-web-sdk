import assert from 'node:assert/strict';
import { test } from 'node:test';
import {
  MAX_ASSET_BYTES, DesktopAdapterError, requireDesktop, invoke, on, readAsset,
  assetBytes, assetText, assetJson
} from '../../sdk/desktop/frontend/desktop.mjs';

const asset = (value) => {
  const bytes = Buffer.from(value);
  return { mime: 'application/octet-stream', size: bytes.length, base64: bytes.toString('base64') };
};
const adapterError = (code) => (error) =>
  error instanceof DesktopAdapterError && error.code === `desktop_adapter_${code}`;

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

test('asset decoding accepts canonical payloads through the exact size cap', () => {
  for (const length of [0, 1, 2, 3, 4, MAX_ASSET_BYTES]) {
    const bytes = Buffer.alloc(length, 197);
    assert.deepEqual(assetBytes(asset(bytes)), new Uint8Array(bytes));
  }
  assert.equal(assetText(asset('中文 🦊')), '中文 🦊');
  assert.deepEqual(assetJson(asset('{"ready":true}')), { ready: true });
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
