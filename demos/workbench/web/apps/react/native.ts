import {
  SouiBridgeError,
  SouiNativeClientImpl,
  type SouiNativeEvent,
  type SouiNativePort,
} from '../../packages/sdk/soui_native_client';

type NativeBootstrapResult = {
  sessionId?: string;
  origin?: string;
  token?: string;
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
  host.__souiNativeDispatchEvent = (value) => {
    if (value && typeof value === 'object' && typeof value.sessionId === 'string' && typeof value.sequence === 'number') {
      port.dispatch(value);
    }
  };
  return new SouiNativeClientImpl(port, {
    sessionId: bootstrap.sessionId,
    origin: bootstrap.origin,
    token: bootstrap.token,
  });
}
