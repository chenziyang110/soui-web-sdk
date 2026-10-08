import { JSDOM } from 'jsdom';
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

export function prepareDOM() {
  const dom = new JSDOM('<!doctype html><html><body></body></html>', { url: 'https://binding.test/' });
  for (const key of ['window', 'document', 'Node', 'Element', 'HTMLElement', 'SVGElement', 'Event', 'MouseEvent', 'navigator'])
    Object.defineProperty(globalThis, key, { configurable: true, writable: true, value: dom.window[key] });
  globalThis.IS_REACT_ACT_ENVIRONMENT = true;
  return dom;
}
export function deferred() {
  let resolve, reject;
  const promise = new Promise((yes, no) => { resolve = yes; reject = no; });
  return { promise, resolve, reject };
}
export const flush = () => new Promise(resolve => setImmediate(resolve));
export async function installed(profile, name) {
  const root = fileURLToPath(profile);
  const directory = path.join(root, 'node_modules', name);
  const metadata = JSON.parse(fs.readFileSync(path.join(directory, 'package.json'), 'utf8'));
  return import(pathToFileURL(path.join(directory, metadata.main)).href);
}
export function nativeFixture(t, sdk) {
  const methods = [];
  const requests = [];
  const work = deferred();
  const client = new sdk.SouiNativeClientImpl({ request: async raw => {
    const request = JSON.parse(raw); methods.push(request.method); requests.push(request);
    if (request.method === 'app.denied') return JSON.stringify({ requestId: request.requestId, ok: false,
      error: { code: 'E_CAPABILITY_DENIED', message: 'Denied by native' } });
    const result = request.method === 'app.work' ? await work.promise : { taskId: 'task-1', state: 'running', cancelRequested: true };
    return JSON.stringify({ requestId: request.requestId, ok: true, result });
  }, onEvent: () => () => {} }, { sessionId: 'binding-session', origin: 'app://binding-test',
    token: 'test-token', requestTimeoutMs: 1000 });
  t.after(() => client.dispose());
  return { client, methods, requests, work };
}
