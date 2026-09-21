import type { SouiMethodMap, SouiMethodName } from './contracts';

export interface SouiNativePort {
  request(envelope: string): Promise<string>;
  onEvent(listener: (event: SouiNativeEvent) => void): () => void;
}

export interface SouiNativeEvent {
  sessionId: string;
  taskId?: string;
  sequence: number;
  eventName: string;
  payload: unknown;
}

export interface SouiBridgeRequest {
  requestId: string;
  sessionId: string;
  origin: string;
  token: string;
  method: string;
  params: unknown;
  timeoutMs: number;
}

export interface SouiBridgeResponse {
  requestId: string;
  ok: boolean;
  result?: unknown;
  error?: { code: string; message: string };
}

export class SouiBridgeError extends Error {
  public readonly code: string;
  public readonly requestId: string;

  public constructor(code: string, message: string, requestId: string) {
    super(message);
    this.name = 'SouiBridgeError';
    this.code = code;
    this.requestId = requestId;
  }
}

export interface SouiNativeClientOptions {
  sessionId: string;
  origin: string;
  token: string;
  requestTimeoutMs?: number;
}

export class SouiNativeClientImpl<Methods extends { [K in keyof Methods]: { params: unknown; result: unknown } } = SouiMethodMap> {
  private nextRequest = 1;

  public constructor(private readonly port: SouiNativePort, private readonly options: SouiNativeClientOptions) {}

  public call<M extends keyof Methods & string>(method: M, params: Methods[M]['params']): Promise<Methods[M]['result']> {
    return this.request<Methods[M]['result']>(method, params);
  }

  private async request<Result>(method: string, params: unknown): Promise<Result> {
    const requestId = `${this.options.sessionId}:${this.nextRequest++}`;
    const request: SouiBridgeRequest = {
      requestId,
      sessionId: this.options.sessionId,
      origin: this.options.origin,
      token: this.options.token,
      method,
      params,
      timeoutMs: this.options.requestTimeoutMs ?? 10000,
    };
    const timeoutMs = request.timeoutMs;
    let timer: ReturnType<typeof setTimeout> | undefined;
    const timeout = new Promise<never>((_, reject) => {
      timer = setTimeout(() => reject(new SouiBridgeError('E_TIMEOUT',
        'The page wait timed out; the native task was not cancelled.', requestId)), timeoutMs);
    });
    let raw: string;
    try {
      raw = await Promise.race([this.port.request(JSON.stringify(request)), timeout]);
    } finally {
      if (timer !== undefined) clearTimeout(timer);
    }
    let response: SouiBridgeResponse;
    try {
      response = JSON.parse(raw) as SouiBridgeResponse;
    } catch {
      throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native response is not valid JSON.', requestId);
    }
    if (response === null || typeof response !== 'object' || Array.isArray(response) ||
        typeof response.ok !== 'boolean' || typeof response.requestId !== 'string') {
      throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native response envelope is invalid.', requestId);
    }
    if (response.requestId !== requestId) {
      throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native response correlation id does not match.', requestId);
    }
    if (!response.ok) {
      const error = response.error ?? { code: 'E_UNKNOWN', message: 'Native request failed.' };
      if (typeof error !== 'object' || typeof error.code !== 'string' || typeof error.message !== 'string') {
        throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native error envelope is invalid.', requestId);
      }
      throw new SouiBridgeError(error.code, error.message, requestId);
    }
    return response.result as Result;
  }

  public query(taskId: string): Promise<SouiMethodMap['task.query']['result']> {
    return this.request('task.query', { taskId });
  }

  public cancel(taskId: string): Promise<SouiMethodMap['task.cancel']['result']> {
    return this.request('task.cancel', { taskId });
  }

  public subscribe(taskId: string, eventName: string, listener: (event: SouiNativeEvent) => void): () => void {
    let lastSequence = 0;
    let nativeSubscriptionId = '';
    let removed = false;
    const removeEventListener = this.port.onEvent((event) => {
      if (event.sessionId !== this.options.sessionId || event.taskId !== taskId || event.eventName !== eventName) {
        return;
      }
      if (event.sequence <= lastSequence) {
        return;
      }
      lastSequence = event.sequence;
      listener(event);
    });
    void this.request<SouiMethodMap['task.subscribe']['result']>('task.subscribe', { taskId, eventName }).then((result) => {
      nativeSubscriptionId = result.subscriptionId;
      if (removed && nativeSubscriptionId) {
        void this.request('task.unsubscribe', { subscriptionId: nativeSubscriptionId }).catch(() => undefined);
      }
    }).catch(() => removeEventListener());
    return () => {
      removed = true;
      removeEventListener();
      if (nativeSubscriptionId) {
        void this.request('task.unsubscribe', { subscriptionId: nativeSubscriptionId }).catch(() => undefined);
      }
    };
  }
}

export type { SouiMethodMap, SouiMethodName };
