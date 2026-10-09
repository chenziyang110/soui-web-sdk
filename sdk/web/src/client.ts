import type { SouiMethodMap } from './contracts.js';
import { describeOptions, discoverCapabilities, readCapabilityPage } from './capabilities.js';
import type { SouiCapabilityCatalog, SouiCapabilityPage, SouiDescribeOptions, SouiDiscoveryOptions } from './capabilities.js';
import { SouiBridgeError, MAX_MESSAGE_BYTES, integerOption, isNativeEvent, isRecord,
  nonEmptyString, readResponse, utf8Bytes } from './protocol.js';
import type { SouiBridgeRequest, SouiNativeEvent, SouiNativePort, SouiSessionLease } from './protocol.js';
import { SessionRenewal } from './session-renewal.js';
import { readTaskQueryParams } from './task-query.js';
import type { SouiCoreMethodMap } from './protocol.js';
import { GuiCommandRoute } from './gui-command.js';
import type { SouiGuiBinding } from './gui-command.js';
import type { SouiGui } from './gui.js';

export interface SouiNativeClientOptions {
  sessionId: string;
  origin: string;
  token: string;
  lease?: SouiSessionLease;
  /** Elapsed bootstrap round-trip time; this can only shorten the local expiry estimate. */
  leaseElapsedMs?: number;
  requestTimeoutMs?: number;
  maxPendingRequests?: number;
  maxSubscriptions?: number;
  maxMessageBytes?: number;
  onError?: (error: SouiBridgeError) => void;
}

export interface SouiSubscription {
  /** Compatibility form: start removal without waiting for its native acknowledgement. */
  (): void;
  readonly ready: Promise<void>;
  readonly closed: boolean;
  unsubscribe(): Promise<void>;
}

interface PendingRequest {
  control: boolean;
  reject(error: SouiBridgeError): void;
}
interface SubscriptionState {
  taskId: string;
  eventName: string;
  lastSequence: number;
  removed: boolean;
  listener(event: SouiNativeEvent): void;
  remove(): Promise<void>;
}

let nextClient = 1;

export class SouiNativeClientImpl<Methods extends { [K in keyof Methods]: { params: unknown; result: unknown } } = SouiMethodMap> {
  private readonly clientId = nextClient++;
  private nextRequest = 1;
  private readonly options: SouiNativeClientOptions;
  private readonly timeoutMs: number;
  private readonly requestLimit: number;
  private readonly subscriptionLimit: number;
  private readonly messageLimit: number;
  private readonly pending = new Map<string, PendingRequest>();
  private readonly subscriptions = new Set<SubscriptionState>();
  private readonly removeEventListener: () => void;
  private readonly session: SessionRenewal;
  private stopped = false;
  private guiRoute: GuiCommandRoute | undefined;

  public constructor(private readonly port: SouiNativePort, options: SouiNativeClientOptions) {
    if (!options || !nonEmptyString(options.sessionId, 256) || !nonEmptyString(options.origin) ||
        !nonEmptyString(options.token) || (options.onError !== undefined && typeof options.onError !== 'function'))
      throw new SouiBridgeError('E_INVALID_OPTIONS', 'A bounded session, origin and token are required.');
    this.options = { ...options };
    this.timeoutMs = integerOption(options.requestTimeoutMs, 10000, 60000, 'requestTimeoutMs');
    this.requestLimit = integerOption(options.maxPendingRequests, 128, 1024, 'maxPendingRequests');
    this.subscriptionLimit = integerOption(options.maxSubscriptions, 128, 1024, 'maxSubscriptions');
    this.messageLimit = integerOption(options.maxMessageBytes, MAX_MESSAGE_BYTES, MAX_MESSAGE_BYTES, 'maxMessageBytes');
    this.session = new SessionRenewal(port, this.options, this.clientId, () => this.newRequestId(),
      this.timeoutMs, this.messageLimit, error => this.report(error));
    try {
      this.removeEventListener = port.onEvent(event => this.receiveEvent(event));
      if (typeof this.removeEventListener !== 'function')
        throw new SouiBridgeError('E_INVALID_PORT', 'The event port must return a listener removal function.');
    } catch (error) { this.session.dispose(); throw error; }
  }

  public get disposed(): boolean { return this.stopped; }
  public get pendingRequestCount(): number { return this.pending.size + this.session.pendingRequestCount; }
  public get subscriptionCount(): number { return this.subscriptions.size; }
  /** Conservative local estimate; does not prove that native authorization has not been revoked. */
  public get sessionLease(): SouiSessionLease | undefined { return this.session.lease; }

  /** Shares an in-flight renewal. Before the renewal window, resolves the current lease without sending. */
  public renewSession(): Promise<SouiSessionLease> { return this.session.renew(); }

  /** Page route only. Native readiness and Agent authority remain native decisions. */
  public bindGui(gui: SouiGui): SouiGuiBinding {
    if (this.stopped || gui.disposed) throw new SouiBridgeError('E_DISPOSED', 'The GUI route is closed.');
    if (this.guiRoute) throw new SouiBridgeError('E_BUSY', 'This client already has a GUI route.');
    const route = new GuiCommandRoute(gui, (method, params) => this.request(method, params),
      () => !this.stopped, error => this.report(error));
    this.guiRoute = route;
    return route;
  }

  public call<M extends keyof Methods & string>(method: M, params: Methods[M]['params']): Promise<Methods[M]['result']> {
    if (method === 'bridge.session.renew')
      return Promise.reject(new SouiBridgeError('E_RESERVED_METHOD', 'Use renewSession() to update this client authorization.'));
    return this.request<Methods[M]['result']>(method, params);
  }

  /** Read one authorized page. Older hosts return their unsupported-method error. */
  public async describe(options: SouiDescribeOptions = {}): Promise<SouiCapabilityPage> {
    const { cursor, limit } = describeOptions(options);
    return readCapabilityPage(await this.request<unknown>('bridge.describe', { cursor, limit }), cursor, limit);
  }

  /** Read a bounded catalog without invoking the discovered business methods. */
  public discover(options: SouiDiscoveryOptions = {}): Promise<SouiCapabilityCatalog> {
    return discoverCapabilities(page => this.describe(page), options);
  }

  private newRequestId(): string { return `${this.options.sessionId}:${this.clientId}:${this.nextRequest++}`; }

  private report(error: unknown, code = 'E_TRANSPORT'): void {
    const detail = error instanceof SouiBridgeError ? error : new SouiBridgeError(code,
      error instanceof Error ? error.message : 'The native operation failed.');
    try { this.options.onError?.(detail); } catch { /* An observer cannot break cleanup or other callbacks. */ }
  }

  private request<Result>(method: string, params: unknown, control = false,
                          lateSuccess?: (value: Result) => void): Promise<Result> {
    const requestId = this.newRequestId();
    if (this.stopped && !control)
      return Promise.reject(new SouiBridgeError('E_DISPOSED', 'The native client has been disposed.', requestId));
    if (!nonEmptyString(method))
      return Promise.reject(new SouiBridgeError('E_INVALID_PARAMS', 'A method name is required.', requestId));
    if (method === 'task.query') {
      try { params = readTaskQueryParams(params, requestId); }
      catch (error) { return Promise.reject(error); }
    }
    const request: SouiBridgeRequest = { requestId, sessionId: this.options.sessionId, origin: this.options.origin,
      token: this.session.token, method, params, timeoutMs: this.timeoutMs };
    let envelope: string;
    try { envelope = JSON.stringify(request); }
    catch { return Promise.reject(new SouiBridgeError('E_INVALID_PARAMS', 'Request parameters are not JSON serializable.', requestId)); }
    if (utf8Bytes(envelope) > this.messageLimit)
      return Promise.reject(new SouiBridgeError('E_LIMIT_MESSAGE_SIZE', 'The client message byte limit was exceeded.', requestId));
    // JSON conversion can execute application toJSON methods and reenter this client.
    if (this.stopped && !control)
      return Promise.reject(new SouiBridgeError('E_DISPOSED', 'The native client has been disposed.', requestId));
    const count = Array.from(this.pending.values()).filter(item => item.control === control).length;
    if (count >= (control ? this.subscriptionLimit : this.requestLimit))
      return Promise.reject(new SouiBridgeError('E_BUSY', 'The client request capacity is exhausted.', requestId));

    return new Promise<Result>((resolve, reject) => {
      let waiting = true;
      const finish = () => {
        waiting = false;
        clearTimeout(timer);
        this.pending.delete(requestId);
      };
      const fail = (error: unknown) => {
        if (!waiting) return;
        finish();
        reject(error instanceof SouiBridgeError ? error :
          new SouiBridgeError('E_TRANSPORT', 'The native transport failed.', requestId));
      };
      const timer = setTimeout(() => fail(new SouiBridgeError('E_TIMEOUT',
        'The page wait timed out; the native task was not cancelled.', requestId)), this.timeoutMs);
      this.pending.set(requestId, { control, reject: fail });
      let response: Promise<string>;
      try { response = this.port.request(envelope); }
      catch (error) { fail(error); return; }
      Promise.resolve(response).then(raw => {
        let value: Result;
        try { value = readResponse(raw, requestId).result as Result; }
        catch (error) { fail(error); return; }
        if (!waiting) {
          // A timed-out or disposed subscription can still have been created natively.
          try { lateSuccess?.(value); } catch (error) { this.report(error); }
          return;
        }
        finish();
        resolve(value);
      }, fail);
    });
  }

  /** Compatibility helpers require a native task service registered by the application. */
  public query(taskId: string): Promise<SouiMethodMap['task.query']['result']> {
    return this.request('task.query', { taskId });
  }
  /** Read the current scope's retained operation; never restarts or cancels it. */
  public queryOperation(operationId: string): Promise<SouiCoreMethodMap['task.query']['result']> {
    return this.request('task.query', { operationId });
  }
  public cancel(taskId: string): Promise<SouiMethodMap['task.cancel']['result']> {
    return this.request('task.cancel', { taskId });
  }

  public subscribe(taskId: string, eventName: string, listener: (event: SouiNativeEvent) => void): SouiSubscription {
    if (this.stopped) throw new SouiBridgeError('E_DISPOSED', 'The native client has been disposed.');
    if (!nonEmptyString(taskId) || !nonEmptyString(eventName) || typeof listener !== 'function')
      throw new SouiBridgeError('E_INVALID_PARAMS', 'A task, event name and listener are required.');
    if (this.subscriptions.size >= this.subscriptionLimit)
      throw new SouiBridgeError('E_BUSY', 'The client subscription capacity is exhausted.');
    let nativeId = '';
    let nativeRemoval: Promise<void> | undefined;
    let removal: Promise<void> | undefined;
    let ready: Promise<void>;
    const state: SubscriptionState = { taskId, eventName, listener, lastSequence: 0, removed: false,
      remove: () => remove() };
    const detach = () => { state.removed = true; this.subscriptions.delete(state); };
    const removeNative = (): Promise<void> => {
      if (!nativeRemoval) {
        nativeRemoval = this.request<unknown>('task.unsubscribe', { subscriptionId: nativeId }, true).then(result => {
          if (!isRecord(result) || result.removed !== true)
            throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native subscription removal was not acknowledged.');
        });
        void nativeRemoval.catch(error => this.report(error));
      }
      return nativeRemoval;
    };
    const established = (result: unknown) => {
      if (!isRecord(result) || !nonEmptyString(result.subscriptionId))
        throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native subscription identity is invalid.');
      nativeId = result.subscriptionId;
      if (state.removed) void removeNative();
    };
    const remove = (): Promise<void> => {
      detach();
      if (!removal) removal = nativeId ? removeNative() : Promise.resolve().then(() => ready).then(removeNative);
      void removal.catch(() => undefined);
      return removal;
    };
    this.subscriptions.add(state);
    ready = this.request<unknown>('task.subscribe', { taskId, eventName }, false, established)
      .then(established).catch(error => { detach(); throw error; });
    // Keep legacy callers safe from unhandled rejection, while ready remains observable.
    void ready.catch(error => this.report(error));
    const handle = (() => { void remove(); }) as SouiSubscription;
    Object.defineProperties(handle, { ready: { value: ready }, closed: { get: () => state.removed } });
    handle.unsubscribe = remove;
    return handle;
  }

  private receiveEvent(event: unknown): void {
    if (this.stopped || !isNativeEvent(event) || event.sessionId !== this.options.sessionId) return;
    if (event.eventName === 'gui.command') { this.guiRoute?.receive(event); return; }
    for (const state of Array.from(this.subscriptions)) {
      if (state.removed || state.taskId !== event.taskId || state.eventName !== event.eventName ||
          event.sequence <= state.lastSequence) continue;
      state.lastSequence = event.sequence;
      try { state.listener(event); } catch (error) { this.report(error, 'E_EVENT_LISTENER'); }
    }
  }

  /** Immediate local cleanup. Native task cancellation and acknowledged unsubscription are separate operations. */
  public dispose(): void {
    if (this.stopped) return;
    this.stopped = true;
    this.guiRoute?.dispose();
    this.session.dispose();
    try { this.removeEventListener(); } catch (error) { this.report(error); }
    for (const state of Array.from(this.subscriptions)) void state.remove();
    for (const [requestId, item] of Array.from(this.pending)) {
      if (!item.control) item.reject(new SouiBridgeError('E_DISPOSED',
        'The page wait ended because the client was disposed; native work was not cancelled.', requestId));
    }
    try { this.port.dispose?.(); } catch (error) { this.report(error); }
  }
}
