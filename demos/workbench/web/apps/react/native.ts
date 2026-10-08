import {
  SouiBridgeError,
  SouiNativeClientImpl,
  type SouiNativeEvent,
  type SouiNativePort,
  type SouiSessionLease,
} from '../../packages/sdk/soui_native_client';

type NativeBootstrapResult = {
  sessionId?: string;
  origin?: string;
  token?: string;
  lease?: SouiSessionLease;
  benchmarkRootToken?: string;
  sampleRootToken?: string;
  alternateSampleRootToken?: string;
};
type NativeHost = Window & {
  __souiNativePostMessage?: (envelope: string) => string;
  __souiNativeDispatchEvent?: (event: SouiNativeEvent) => void;
  __souiFileInsightSampleRootToken?: string;
  __souiFileInsightAlternateSampleRootToken?: string;
  __souiBenchmarkRoot?: string;
};

const host = window as NativeHost;

class SynchronousNativePort implements SouiNativePort {
  private readonly listeners = new Set<(event: SouiNativeEvent) => void>();
  private releaseDocument?: () => void;

  bindDocument(client: SouiNativeClientImpl, dispatch: (event: SouiNativeEvent) => void): void {
    const previous = host.__souiNativeDispatchEvent;
    const close = () => client.dispose();
    this.releaseDocument = () => {
      host.removeEventListener('pagehide', close);
      host.removeEventListener('beforeunload', close);
      if (host.__souiNativeDispatchEvent === dispatch) {
        if (previous) host.__souiNativeDispatchEvent = previous;
        else delete host.__souiNativeDispatchEvent;
      }
    };
    host.__souiNativeDispatchEvent = dispatch;
    host.addEventListener('pagehide', close);
    host.addEventListener('beforeunload', close);
  }

  dispose(): void {
    this.listeners.clear();
    const release = this.releaseDocument;
    this.releaseDocument = undefined;
    release?.();
  }

  request(envelope: string): Promise<string> {
    if (typeof host.__souiNativePostMessage !== 'function') {
      return Promise.reject(new SouiBridgeError('E_HOST_UNAVAILABLE', 'SOUI/Miniblink host is unavailable.', ''));
    }
    try { return Promise.resolve(host.__souiNativePostMessage(envelope)); }
    catch (error) { return Promise.reject(error); }
  }

  onEvent(listener: (event: SouiNativeEvent) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  dispatch(event: SouiNativeEvent): void {
    for (const listener of this.listeners) listener(event);
  }
}

function parseBootstrap(raw: string, requestId: string): NativeBootstrapResult {
  let response: { requestId?: string; ok?: boolean; result?: NativeBootstrapResult; error?: { code?: string; message?: string } };
  try { response = JSON.parse(raw) as typeof response; }
  catch { throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native bootstrap response is not valid JSON.', requestId); }
  if (response.requestId !== requestId) throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native bootstrap correlation id mismatch.', requestId);
  if (!response.ok) throw new SouiBridgeError(response.error?.code ?? 'E_HOST_UNAVAILABLE', response.error?.message ?? 'Native bootstrap failed.', requestId);
  return response.result ?? {};
}

export function createReactNativeClient(): SouiNativeClientImpl | null {
  if (typeof host.__souiNativePostMessage !== 'function') return null;
  const requestId = 'react-bootstrap:1';
  const bootstrapStartedAt = performance.now();
  const raw = host.__souiNativePostMessage(JSON.stringify({
    requestId, sessionId: '', origin: '', token: '', method: 'bridge.bootstrap', params: { protocol: 'soui.bridge/1' },
  }));
  const bootstrap = parseBootstrap(raw, requestId);
  if (!bootstrap.sessionId || !bootstrap.origin || !bootstrap.token) {
    throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native bootstrap did not issue a complete session.', requestId);
  }
  (window as Window & { __souiReactSessionId?: string }).__souiReactSessionId = bootstrap.sessionId;
  if (import.meta.env.MODE === 'performance') {
    host.__souiBenchmarkRoot = bootstrap.benchmarkRootToken || '';
  }
  host.__souiFileInsightSampleRootToken = bootstrap.sampleRootToken || '';
  host.__souiFileInsightAlternateSampleRootToken = bootstrap.alternateSampleRootToken || '';
  const port = new SynchronousNativePort();
  const client = new SouiNativeClientImpl(port, {
    sessionId: bootstrap.sessionId,
    origin: bootstrap.origin,
    token: bootstrap.token,
    lease: bootstrap.lease,
    leaseElapsedMs: bootstrap.lease === undefined ? 0 : performance.now() - bootstrapStartedAt,
  });
  port.bindDocument(client, value => {
    if (value && typeof value === 'object' && typeof value.sessionId === 'string' && typeof value.sequence === 'number') {
      port.dispatch(value);
    }
  });
  return client;
}
