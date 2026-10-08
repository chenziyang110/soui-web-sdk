import test from 'node:test';
import assert from 'node:assert/strict';
import { SouiNativeClientImpl } from '../../sdk/web/dist/index.js';

test('Agent discovery uses renewed authorization and shares unique identities with renewal', async t => {
  const credentials = { sessionId: 'agent-session', origin: 'app://agent', token: 'token-1' };
  const requests = [];
  const client = new SouiNativeClientImpl({
    onEvent: () => () => {},
    request: async envelope => {
      const request = JSON.parse(envelope);
      requests.push(request);
      let result;
      if (request.method === 'bridge.session.renew') {
        result = { ...credentials, token: 'token-2',
          lease: { generation: 2, expiresInMs: 1800000, renewAfterMs: 1500000 } };
      } else {
        assert.equal(request.method, 'bridge.describe');
        const cursor = request.params.cursor;
        result = { protocol: 'soui.capabilities/1', sdkVersion: '0.4.0-preview.2',
          methods: [{ name: cursor === 0 ? 'app.a' : 'app.b', capability: 'agent.read' }],
          nextCursor: cursor === 0 ? 1 : null };
      }
      return JSON.stringify({ requestId: request.requestId, ok: true, result });
    },
  }, { ...credentials, lease: { generation: 1, expiresInMs: 300000, renewAfterMs: 0 } });
  t.after(() => client.dispose());

  await client.renewSession();
  const catalog = await client.discover({ pageSize: 1 });
  assert.deepEqual(catalog.methods.map(method => method.name), ['app.a', 'app.b']);
  assert.deepEqual(requests.map(request => [request.method, request.token]), [
    ['bridge.session.renew', 'token-1'],
    ['bridge.describe', 'token-2'],
    ['bridge.describe', 'token-2'],
  ]);
  assert.equal(new Set(requests.map(request => request.requestId)).size, 3);
  assert.ok(requests.every(request => request.sessionId === credentials.sessionId &&
    request.origin === credentials.origin));
  assert.equal(client.pendingRequestCount, 0);
});
