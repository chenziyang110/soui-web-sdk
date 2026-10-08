import { SouiBridgeError, isRecord, nonEmptyString, readResponse, readSessionLease, utf8Bytes } from './protocol.js';
import type { SouiNativePort, SouiSessionLease } from './protocol.js';

type Timer = ReturnType<typeof setTimeout>;
interface Credentials {
  sessionId: string;
  origin: string;
  token: string;
  lease?: SouiSessionLease;
  leaseElapsedMs?: number;
}
interface Renewal {
  operationId: string;
  generation: number;
  token: string;
  deadline: number;
  attempts: number;
  timer?: Timer;
  retryTimer?: Timer;
  promise: Promise<SouiSessionLease>;
  resolve(lease: SouiSessionLease): void;
  reject(error: SouiBridgeError): void;
}

// performance.now is available in the Chrome 49 target and is unaffected by
// wall-clock corrections. Keep timestamps private; the public lease is relative.
export const monotonicNow = (): number => performance.now();

/** Private single-flight control lane; it never queues or replays business calls. */
export class SessionRenewal {
  public token: string;
  private generation = 0;
  private expiresAt = 0;
  private renewAt = 0;
  private nextOperation = 1;
  private automaticTimer?: Timer;
  private active?: Renewal;
  private stopped = false;
  private failure?: SouiBridgeError;

  public constructor(private readonly port: SouiNativePort, private readonly credentials: Credentials,
                     private readonly clientId: number, private readonly requestId: () => string,
                     private readonly timeoutMs: number, private readonly messageLimit: number,
                     private readonly report: (error: SouiBridgeError) => void) {
    this.token = credentials.token;
    const elapsed = credentials.leaseElapsedMs ?? 0;
    if (!Number.isFinite(elapsed) || elapsed < 0 || (credentials.lease === undefined && elapsed !== 0))
      throw new SouiBridgeError('E_INVALID_OPTIONS', 'leaseElapsedMs must be a nonnegative elapsed bootstrap time.');
    if (credentials.lease === undefined) return;
    const lease = readSessionLease(credentials.lease, 'E_INVALID_OPTIONS');
    const now = monotonicNow();
    this.generation = lease.generation;
    this.expiresAt = now + lease.expiresInMs - Math.ceil(elapsed);
    if (this.expiresAt <= now)
      throw new SouiBridgeError('E_SESSION_EXPIRED', 'The bootstrap authorization has already expired locally.');
    // Waiting the native relative renewal interval from receipt avoids minting
    // too early. Only expiration uses the earlier send-time estimate.
    this.renewAt = Math.min(now + lease.renewAfterMs, this.expiresAt);
    this.schedule();
  }

  public get pendingRequestCount(): number { return this.active ? 1 : 0; }
  public get lease(): SouiSessionLease | undefined {
    if (!this.generation) return undefined;
    const now = monotonicNow();
    const remaining = Math.max(0, Math.floor(this.expiresAt - now));
    return { generation: this.generation, expiresInMs: remaining,
      renewAfterMs: Math.min(remaining, Math.max(0, Math.ceil(this.renewAt - now))) };
  }

  public renew(): Promise<SouiSessionLease> {
    if (this.stopped) return Promise.reject(new SouiBridgeError('E_DISPOSED', 'The native client has been disposed.'));
    if (!this.generation)
      return Promise.reject(new SouiBridgeError('E_RENEWAL_UNAVAILABLE', 'The native application did not enable session renewal.'));
    if (this.active) return this.active.promise;
    if (this.failure) return Promise.reject(this.failure);
    const now = monotonicNow();
    if (now >= this.expiresAt)
      return Promise.reject(new SouiBridgeError('E_SESSION_EXPIRED', 'The document authorization has expired; reload is required.'));
    if (now < this.renewAt) return Promise.resolve(this.lease!);
    this.clearAutomatic();
    let resolve!: Renewal['resolve'], reject!: Renewal['reject'];
    const promise = new Promise<SouiSessionLease>((yes, no) => { resolve = yes; reject = no; });
    const operation: Renewal = { operationId: `renew:${this.clientId}:${this.nextOperation++}`,
      generation: this.generation, token: this.token, deadline: Math.min(now + 25000, this.expiresAt),
      attempts: 0, promise, resolve, reject };
    // Publish before calling the port: even synchronous reentrancy shares this flight.
    this.active = operation;
    this.attempt(operation);
    return promise;
  }

  private schedule(): void {
    this.clearAutomatic();
    if (this.stopped || this.failure || !this.generation) return;
    this.automaticTimer = setTimeout(() => {
      this.automaticTimer = undefined;
      // Timer resolution can fire before the monotonic deadline. Keep ownership
      // of a later timer instead of consuming the only automatic attempt.
      if (monotonicNow() < this.renewAt) { this.schedule(); return; }
      void this.renew().catch(error => { if (!this.stopped) this.report(error); });
    }, Math.max(0, Math.ceil(this.renewAt - monotonicNow())));
  }

  private clearAutomatic(): void {
    if (this.automaticTimer !== undefined) clearTimeout(this.automaticTimer);
    this.automaticTimer = undefined;
  }

  private isCurrent(operation: Renewal): boolean { return !this.stopped && this.active === operation; }

  private attempt(operation: Renewal): void {
    if (!this.isCurrent(operation)) return;
    const sentAt = monotonicNow();
    const remaining = Math.floor(operation.deadline - sentAt);
    if (remaining < 1) {
      this.fail(operation, new SouiBridgeError('E_RENEWAL_TIMEOUT', 'Authorization renewal exceeded its recovery deadline.'));
      return;
    }
    const requestId = this.requestId();
    const timeout = Math.min(this.timeoutMs, 10000, remaining);
    const envelope = JSON.stringify({ requestId, sessionId: this.credentials.sessionId,
      origin: this.credentials.origin, token: operation.token, method: 'bridge.session.renew',
      params: { operationId: operation.operationId }, timeoutMs: timeout });
    if (utf8Bytes(envelope) > this.messageLimit) {
      this.fail(operation, new SouiBridgeError('E_LIMIT_MESSAGE_SIZE', 'The renewal message exceeds the client byte limit.', requestId));
      return;
    }
    ++operation.attempts;
    let waiting = true;
    const failedAttempt = (error: unknown) => {
      if (!waiting || !this.isCurrent(operation)) return;
      waiting = false;
      if (operation.timer !== undefined) clearTimeout(operation.timer);
      operation.timer = undefined;
      const detail = error instanceof SouiBridgeError ? error :
        new SouiBridgeError('E_TRANSPORT', 'The native renewal transport failed.', requestId);
      const retryable = ['E_TIMEOUT', 'E_TRANSPORT', 'E_BUSY', 'E_RENEW_TOO_EARLY'].indexOf(detail.code) !== -1;
      // Keep the last send for the end of this fixed recovery window. Native
      // work can still occupy its slot after its JS resolver has timed out;
      // an immediate third send would only consume the final retry on E_BUSY.
      const delay = operation.attempts === 1 ? 250 : Math.max(500,
        Math.ceil(operation.deadline - monotonicNow() - Math.min(this.timeoutMs, 5000)));
      if (retryable && operation.attempts < 3 && monotonicNow() + delay < operation.deadline) {
        operation.retryTimer = setTimeout(() => {
          operation.retryTimer = undefined;
          this.attempt(operation);
        }, delay);
      } else if (retryable) this.waitForRecovery(operation, detail);
      else this.fail(operation, detail);
    };
    operation.timer = setTimeout(() => failedAttempt(new SouiBridgeError('E_TIMEOUT',
      'The renewal acknowledgement timed out; native rotation may already have occurred.', requestId)), timeout);
    let response: Promise<string>;
    try { response = this.port.request(envelope); }
    catch (error) { failedAttempt(error); return; }
    Promise.resolve(response).then(raw => {
      if (!this.isCurrent(operation)) return;
      if (monotonicNow() >= operation.deadline) {
        this.fail(operation, new SouiBridgeError('E_RENEWAL_TIMEOUT', 'A renewal response arrived after its recovery deadline.', requestId));
        return;
      }
      try {
        const value = readResponse(raw, requestId).result;
        if (!isRecord(value) || value.sessionId !== this.credentials.sessionId || value.origin !== this.credentials.origin ||
            !nonEmptyString(value.token) || value.token === operation.token)
          throw new SouiBridgeError('E_INVALID_RESPONSE', 'Renewal changed the document identity or returned invalid credentials.', requestId);
        const lease = readSessionLease(value.lease);
        if (lease.generation !== operation.generation + 1 || this.generation !== operation.generation)
          throw new SouiBridgeError('E_INVALID_RESPONSE', 'Renewal returned an unexpected authorization generation.', requestId);
        const expiresAt = sentAt + lease.expiresInMs;
        const receivedAt = monotonicNow();
        if (expiresAt <= receivedAt)
          throw new SouiBridgeError('E_SESSION_EXPIRED', 'The renewed credentials expired before acknowledgement.', requestId);
        // Any still-active attempt may recover this exact operation, including a
        // timed-out first attempt. Completed flights and disposed clients cannot.
        this.token = value.token;
        this.generation = lease.generation;
        this.expiresAt = expiresAt;
        this.renewAt = Math.min(receivedAt + lease.renewAfterMs, expiresAt);
        this.clearFlight(operation);
        operation.resolve(this.lease!);
        this.schedule();
      } catch (error) {
        // A stale failure cannot defeat a newer attempt of the same operation.
        if (waiting) failedAttempt(error);
      }
    }, failedAttempt);
  }

  private waitForRecovery(operation: Renewal, error: SouiBridgeError): void {
    if (!this.isCurrent(operation)) return;
    const remaining = operation.deadline - monotonicNow();
    if (remaining <= 0) { this.fail(operation, error); return; }
    // No fourth request. A custom port may still deliver an earlier promise;
    // the built-in resolver's completed timeout cannot be revived this way.
    operation.timer = setTimeout(() => {
      operation.timer = undefined;
      this.waitForRecovery(operation, error);
    }, Math.ceil(remaining));
  }

  private clearFlight(operation: Renewal): void {
    if (operation.timer !== undefined) clearTimeout(operation.timer);
    if (operation.retryTimer !== undefined) clearTimeout(operation.retryTimer);
    this.active = undefined;
  }

  private fail(operation: Renewal, error: SouiBridgeError): void {
    if (this.active !== operation) return;
    this.clearFlight(operation);
    this.failure = error;
    operation.reject(error);
  }

  public dispose(): void {
    this.stopped = true;
    this.clearAutomatic();
    if (this.active) this.fail(this.active, new SouiBridgeError('E_DISPOSED', 'The client closed during authorization renewal.'));
  }
}
