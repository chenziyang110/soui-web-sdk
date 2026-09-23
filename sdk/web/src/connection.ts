import { SouiNativeClientImpl } from './client.js';
import type { SouiNativeClientOptions } from './client.js';
import { SouiBridgeError, MAX_MESSAGE_BYTES, integerOption, isNativeEvent, isRecord, nonEmptyString, readResponse } from './protocol.js';
import type { SouiCoreMethodMap, SouiNativeEvent, SouiNativePort } from './protocol.js';

export interface SouiNativeWindow {
  __souiNativePostMessage?: (envelope: string) => string | Promise<string>;
  __souiNativeDispatchEvent?: (event: unknown) => void;
  addEventListener(type: string, listener: EventListener): void;
  removeEventListener(type: string, listener: EventListener): void;
}

export interface SouiConnectionOptions extends Omit<SouiNativeClientOptions, 'sessionId' | 'origin' | 'token'> {
  target?: SouiNativeWindow;
  bootstrapTimeoutMs?: number;
}

interface ConnectionRecord {
  promise: Promise<unknown>;
  key: string;
  onError?: SouiNativeClientOptions['onError'];
  closed: boolean;
}

const connections = new WeakMap<SouiNativeWindow, ConnectionRecord>();
let nextBootstrap = 1;

/** Own one connection per document. After failure or disposal, reload before reconnecting. */
export function connectSouiNative<Methods extends { [K in keyof Methods]: { params: unknown; result: unknown } } = SouiCoreMethodMap>(
  options: SouiConnectionOptions = {},
): Promise<SouiNativeClientImpl<Methods>> {
  try {
    const target = options.target ?? (typeof window !== 'undefined' ? window as SouiNativeWindow : undefined);
    if (!target || typeof target.__souiNativePostMessage !== 'function' ||
        typeof target.addEventListener !== 'function' || typeof target.removeEventListener !== 'function')
      throw new SouiBridgeError('E_NATIVE_UNAVAILABLE', 'Open this application in its SOUI desktop host.');
    const limits = {
      requestTimeoutMs: integerOption(options.requestTimeoutMs, 10000, 60000, 'requestTimeoutMs'),
      maxPendingRequests: integerOption(options.maxPendingRequests, 128, 1024, 'maxPendingRequests'),
      maxSubscriptions: integerOption(options.maxSubscriptions, 128, 1024, 'maxSubscriptions'),
      maxMessageBytes: integerOption(options.maxMessageBytes, MAX_MESSAGE_BYTES, MAX_MESSAGE_BYTES, 'maxMessageBytes'),
    };
    const bootstrapTimeoutMs = integerOption(options.bootstrapTimeoutMs, 10000, 120000, 'bootstrapTimeoutMs');
    if (options.onError !== undefined && typeof options.onError !== 'function')
      throw new SouiBridgeError('E_INVALID_OPTIONS', 'onError must be a function.');
    const key = JSON.stringify({ ...limits, bootstrapTimeoutMs });
    const existing = connections.get(target);
    if (existing) {
      if (existing.closed) throw new SouiBridgeError('E_DISPOSED', 'Reload this document before reconnecting.');
      if (existing.key !== key || existing.onError !== options.onError)
        throw new SouiBridgeError('E_CONNECTION_OPTIONS', 'This document already has a connection with different options.');
      return existing.promise as Promise<SouiNativeClientImpl<Methods>>;
    }
    const record: ConnectionRecord = { key, onError: options.onError, closed: false, promise: Promise.resolve() };
    // Publish ownership before invoking any user-supplied native binding.
    record.promise = Promise.resolve().then(async () => {
      const send = target.__souiNativePostMessage!;
      const previous = Object.getOwnPropertyDescriptor(target, '__souiNativeDispatchEvent');
      const listeners = new Set<(event: SouiNativeEvent) => void>();
      let client: SouiNativeClientImpl<Methods> | undefined;
      let timer: ReturnType<typeof setTimeout> | undefined;
      let abort: (error: SouiBridgeError) => void = () => undefined;
      const dispatch = (event: unknown) => {
        if (record.closed || !isNativeEvent(event)) return;
        for (const listener of Array.from(listeners)) listener(event);
      };
      const close = () => {
        if (record.closed) return;
        record.closed = true;
        listeners.clear();
        target.removeEventListener('pagehide', unload);
        target.removeEventListener('beforeunload', unload);
        if (target.__souiNativeDispatchEvent === dispatch) {
          if (previous) Object.defineProperty(target, '__souiNativeDispatchEvent', previous);
          else delete target.__souiNativeDispatchEvent;
        }
      };
      const unload: EventListener = () => {
        if (client) client.dispose();
        else { abort(new SouiBridgeError('E_DISPOSED', 'The document closed during connection.')); close(); }
      };
      const port: SouiNativePort = {
        request: envelope => {
          // Keep release requests possible after local disposal; the client rejects new business calls.
          try { return Promise.resolve(send.call(target, envelope)); }
          catch (error) { return Promise.reject(error); }
        },
        onEvent: listener => { listeners.add(listener); return () => { listeners.delete(listener); }; },
        dispose: close,
      };
      try {
        if (target.__souiNativeDispatchEvent !== undefined)
          throw new SouiBridgeError('E_CONNECTION_IN_USE', 'Another native event dispatcher owns this document.');
        target.__souiNativeDispatchEvent = dispatch;
        target.addEventListener('pagehide', unload);
        target.addEventListener('beforeunload', unload);
        const interrupted = new Promise<never>((_, reject) => { abort = reject; });
        const requestId = `sdk:bootstrap:${nextBootstrap++}`;
        timer = setTimeout(() => abort(new SouiBridgeError('E_TIMEOUT', 'Native bootstrap timed out; reload before retrying.', requestId)), bootstrapTimeoutMs);
        const raw = await Promise.race([port.request(JSON.stringify({ requestId, sessionId: '', origin: '', token: '',
          method: 'bridge.bootstrap', params: { protocol: 'soui.bridge/1' } })), interrupted]);
        const result = readResponse(raw, requestId).result;
        if (!isRecord(result) || !nonEmptyString(result.sessionId, 256) || !nonEmptyString(result.origin) ||
            !result.origin.startsWith('app://') || !nonEmptyString(result.token))
          throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native bootstrap credentials are invalid.', requestId);
        if (record.closed) throw new SouiBridgeError('E_DISPOSED', 'The document closed during connection.');
        client = new SouiNativeClientImpl<Methods>(port, { sessionId: result.sessionId, origin: result.origin,
          token: result.token, ...limits, onError: record.onError });
        return client;
      } catch (error) {
        close();
        throw error instanceof SouiBridgeError ? error : new SouiBridgeError('E_NATIVE_CONNECTION', 'The native connection failed.');
      } finally { if (timer !== undefined) clearTimeout(timer); }
    });
    connections.set(target, record);
    return record.promise as Promise<SouiNativeClientImpl<Methods>>;
  } catch (error) { return Promise.reject(error); }
}
