import test from 'node:test';
import assert from 'node:assert/strict';
import { SouiNativeClientImpl } from '../../sdk/web/dist/index.js';

function fixture(t, reply) {
  const requests = [];
  const client = new SouiNativeClientImpl({
    request: async raw => {
      const request = JSON.parse(raw);
      requests.push(request);
      const response = await reply(request);
      return JSON.stringify({ requestId: request.requestId, ok: true, result: response });
    },
    onEvent: () => () => {},
  }, { sessionId: 'session', origin: 'app://test', token: 'token', requestTimeoutMs: 100 });
  t.after(() => client.dispose());
  return { client, requests };
}
const method = name => ({ name, capability: name });
const page = (methods = [method('app.echo')], nextCursor = null) => ({
  protocol: 'soui.capabilities/1', sdkVersion: '0.3.0-preview.9', methods, nextCursor,
});

test('describe uses the existing authenticated transport and returns an owned page', async t => {
  const f = fixture(t, () => page());
  assert.deepEqual(await f.client.describe(), page());
  assert.deepEqual(f.requests[0].params, { cursor: 0, limit: 32 });
  assert.equal(f.requests[0].method, 'bridge.describe');
  assert.equal(f.requests[0].sessionId, 'session');
  assert.equal(f.requests[0].origin, 'app://test');
  assert.equal(f.requests[0].token, 'token');
});

test('discovery follows authorized pages without calling business methods', async t => {
  const f = fixture(t, ({ params }) => params.cursor === 0 ?
    page([method('app.a'), method('app.b')], 2) : page([method('app.c')]));
  assert.deepEqual(await f.client.discover({ pageSize: 2 }), {
    protocol: 'soui.capabilities/1', sdkVersion: '0.3.0-preview.9',
    methods: [method('app.a'), method('app.b'), method('app.c')],
  });
  assert.deepEqual(f.requests.map(r => r.params), [{ cursor: 0, limit: 2 }, { cursor: 2, limit: 2 }]);
  assert.ok(f.requests.every(r => r.method === 'bridge.describe'));
});

test('empty complete catalog is valid', async t => {
  const f = fixture(t, () => page([]));
  assert.deepEqual((await f.client.discover()).methods, []);
  assert.equal(f.requests.length, 1);
});

test('describe rejects invalid options before invoking native code', async t => {
  const f = fixture(t, () => page());
  for (const options of [null, [], { cursor: -1 }, { cursor: 1.5 }, { cursor: '0' },
    { cursor: Number.MAX_SAFE_INTEGER + 1 }, { limit: 0 }, { limit: 65 }, { limit: NaN },
    { limit: true }, { userGesture: true }]) {
    await assert.rejects(f.client.describe(options), { code: 'E_INVALID_OPTIONS' });
  }
  assert.equal(f.requests.length, 0);
});

test('describe rejects malformed, oversized or non-progressing native pages', async t => {
  const invalid = [null, [], {}, { ...page(), protocol: 'other' }, { ...page(), sdkVersion: '' },
    { ...page(), methods: {} }, page([method('bad method')]), page([method('x'.repeat(129))]),
    page([{ name: 'app.a', capability: '汉'.repeat(200) }]),
    page([{ name: 'app.a', capability: '' }]), page([method('app.b'), method('app.a')]),
    page([method('app.a'), method('app.a')]), page([], 0), page([method('app.a')], 0),
    page([method('app.a')], 2), page([method('app.a')], '1'),
    { ...page(), nextCursor: undefined }, page([method('app.a'), method('app.b'), method('app.c')])];
  for (const value of invalid) {
    const f = fixture(t, () => value);
    await assert.rejects(f.client.describe({ limit: 2 }), { code: 'E_INVALID_RESPONSE' });
  }
});

test('discovery detects duplicate pages and version drift', async t => {
  for (const second of [page([method('app.a')]), { ...page([method('app.b')]), sdkVersion: '0.4.0' }]) {
    const f = fixture(t, ({ params }) => params.cursor === 0 ? page([method('app.a')], 1) : second);
    await assert.rejects(f.client.discover({ pageSize: 1 }), { code: 'E_INVALID_RESPONSE' });
    assert.equal(f.requests.length, 2);
  }
});

test('discovery enforces its total bound without an extra request', async t => {
  const f = fixture(t, () => page([method('app.a'), method('app.b')], 2));
  await assert.rejects(f.client.discover({ pageSize: 32, maxMethods: 2 }), { code: 'E_LIMIT_CAPABILITIES' });
  assert.deepEqual(f.requests.map(r => r.params), [{ cursor: 0, limit: 2 }]);
});

test('a catalog ending exactly at maxMethods succeeds', async t => {
  const f = fixture(t, () => page([method('app.a'), method('app.b')]));
  assert.equal((await f.client.discover({ maxMethods: 2 })).methods.length, 2);
});

test('discovery options are bounded before transport', async t => {
  const f = fixture(t, () => page());
  for (const options of [null, [], { maxMethods: 0 }, { maxMethods: 4097 },
    { maxMethods: 1.5 }, { pageSize: 65 }, { pageSize: '2' }, { all: true }])
    await assert.rejects(f.client.discover(options), { code: 'E_INVALID_OPTIONS' });
  assert.equal(f.requests.length, 0);
});

test('native unsupported and unauthorized errors are preserved', async t => {
  for (const code of ['E_UNIMPLEMENTED', 'E_UNAUTHORIZED']) {
    const client = new SouiNativeClientImpl({
      request: async raw => JSON.stringify({ requestId: JSON.parse(raw).requestId, ok: false,
        error: { code, message: 'native denial' } }), onEvent: () => () => {},
    }, { sessionId: 's', origin: 'app://test', token: 't' });
    t.after(() => client.dispose());
    await assert.rejects(client.discover(), { code, message: 'native denial' });
  }
});

test('dispose stops a paged discovery instead of returning a partial catalog', async t => {
  const f = fixture(t, () => { f.client.dispose(); return page([method('app.a')], 1); });
  await assert.rejects(f.client.discover({ pageSize: 1 }), { code: 'E_DISPOSED' });
  assert.equal(f.requests.length, 1);
});
