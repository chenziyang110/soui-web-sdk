import { SouiBridgeError, integerOption, isRecord, utf8Bytes } from './protocol.js';
import { copySouiJson } from './action-contracts.js';
import type { SouiJson } from './action-contracts.js';

export interface SouiGuiState {
  enabled?: boolean;
  busy?: boolean;
  state?: { [key: string]: SouiJson };
}
export interface SouiGuiRegistration {
  id: string;
  role: string;
  name: string;
  element: Element;
  actions: Record<string, (input: SouiJson) => unknown | Promise<unknown>>;
  state?: () => SouiGuiState;
}
export interface SouiGuiNode {
  id: string;
  role: string;
  name: string;
  enabled: boolean;
  busy: boolean;
  state: { [key: string]: SouiJson };
  actions: string[];
}
export interface SouiGuiSnapshot { protocol: 'soui.gui/1'; nodes: SouiGuiNode[] }
export interface SouiGuiPerformOptions { id: string; action: string; input?: SouiJson; timeoutMs?: number }
export interface SouiGuiWaitOptions { id: string; state?: { [key: string]: SouiJson }; timeoutMs?: number }

interface Entry extends SouiGuiRegistration { removed: boolean; running: boolean }
interface Wait { entry: Entry; reject(error: SouiBridgeError): void; check?(): void }

function failure(code: string, message: string): SouiBridgeError { return new SouiBridgeError(code, message); }
function own(value: object, key: string): boolean { return Object.prototype.hasOwnProperty.call(value, key); }
function identifier(value: unknown): value is string { return typeof value === 'string' && /^[A-Za-z0-9_.:-]{1,128}$/.test(value); }

function options(raw: unknown, allowed: string[]): void {
  if (!isRecord(raw) || Object.keys(raw).some(key => allowed.indexOf(key) < 0) || !identifier(raw.id))
    throw failure('E_INVALID_PARAMS', 'Expected a registered GUI id and supported options.');
}
function connected(element: Element): boolean {
  const current = element as Element & { isConnected?: boolean };
  if (typeof current.isConnected === 'boolean') return current.isConnected;
  return Boolean(element.ownerDocument && element.ownerDocument.documentElement &&
    element.ownerDocument.documentElement.contains(element));
}
function equal(left: SouiJson, right: SouiJson): boolean {
  if (left === right) return true;
  if (Array.isArray(left) && Array.isArray(right))
    return left.length === right.length && left.every((value, index) => equal(value, right[index]));
  if (!isRecord(left) || !isRecord(right)) return false;
  const keys = Object.keys(left);
  return keys.length === Object.keys(right).length && keys.every(key => own(right, key) && equal(left[key], right[key]));
}

/** Explicit application semantics, without selectors, DOM scraping or user-gesture authority. */
export class SouiGui {
  private readonly entries = new Map<string, Entry>();
  private readonly waits = new Set<Wait>();
  private stopped = false;
  private monitor: ReturnType<typeof setInterval> | undefined;

  public get disposed(): boolean { return this.stopped; }

  public register(registration: SouiGuiRegistration): () => void {
    if (this.stopped) throw failure('E_DISPOSED', 'The GUI registry has been disposed.');
    if (!isRecord(registration) || !identifier(registration.id) || typeof registration.role !== 'string' ||
        registration.role.length === 0 || utf8Bytes(registration.role) > 64 || typeof registration.name !== 'string' ||
        registration.name.length === 0 || utf8Bytes(registration.name) > 1024 || !registration.element ||
        registration.element.nodeType !== 1 || !connected(registration.element) || !isRecord(registration.actions) ||
        Object.keys(registration.actions).length < 1 || Object.keys(registration.actions).length > 32 ||
        Object.keys(registration.actions).some(key => !identifier(key) || typeof registration.actions[key] !== 'function') ||
        (registration.state !== undefined && typeof registration.state !== 'function'))
      throw failure('E_INVALID_PARAMS', 'A connected element and bounded, named application actions are required.');
    // Validate Unicode metadata without serializing or retaining the DOM element.
    copySouiJson({ role: registration.role, name: registration.name }, 2048, 'E_INVALID_PARAMS');
    if (this.entries.has(registration.id)) throw failure('E_GUI_DUPLICATE_ID', 'This GUI id is already registered.');
    if (this.entries.size >= 256) throw failure('E_BUSY', 'The GUI registry is full.');
    const actions: SouiGuiRegistration['actions'] = Object.create(null);
    for (const name of Object.keys(registration.actions)) actions[name] = registration.actions[name];
    if (this.stopped) throw failure('E_DISPOSED', 'The GUI registry has been disposed.');
    if (this.entries.has(registration.id)) throw failure('E_GUI_DUPLICATE_ID', 'This GUI id is already registered.');
    if (Object.keys(actions).some(name => typeof actions[name] !== 'function') || !connected(registration.element))
      throw failure('E_INVALID_PARAMS', 'The registration changed while being read.');
    const entry: Entry = { id: registration.id, role: registration.role, name: registration.name,
      element: registration.element, actions, state: registration.state, removed: false, running: false };
    this.entries.set(entry.id, entry);
    return () => {
      if (entry.removed) return;
      entry.removed = true;
      if (this.entries.get(entry.id) === entry) this.entries.delete(entry.id);
      for (const wait of Array.from(this.waits)) if (wait.entry === entry)
        wait.reject(failure('E_GUI_REMOVED', 'The GUI registration was removed; application work was not cancelled.'));
    };
  }

  private active(entry: Entry): void {
    if (this.stopped) throw failure('E_DISPOSED', 'The GUI registry has been disposed.');
    if (entry.removed || this.entries.get(entry.id) !== entry) throw failure('E_GUI_REMOVED', 'The GUI registration was removed.');
    if (!connected(entry.element)) throw failure('E_GUI_DISCONNECTED', 'The registered element is disconnected.');
  }
  private lookup(id: string): Entry {
    if (this.stopped) throw failure('E_DISPOSED', 'The GUI registry has been disposed.');
    const entry = this.entries.get(id);
    if (!entry) throw failure('E_GUI_NOT_FOUND', 'The GUI id is not registered.');
    this.active(entry);
    return entry;
  }
  private node(entry: Entry): SouiGuiNode {
    this.active(entry);
    let supplied: SouiGuiState;
    try { supplied = entry.state ? entry.state() : {}; }
    catch (error) { throw error instanceof SouiBridgeError ? error : failure('E_GUI_STATE', 'The application state provider failed.'); }
    if (!isRecord(supplied) || Object.keys(supplied).some(key => ['enabled', 'busy', 'state'].indexOf(key) < 0))
      throw failure('E_GUI_STATE', 'The application state provider returned an invalid state.');
    const enabled = supplied.enabled;
    const busy = supplied.busy;
    const rawState = supplied.state;
    if ((enabled !== undefined && typeof enabled !== 'boolean') || (busy !== undefined && typeof busy !== 'boolean') ||
        (rawState !== undefined && !isRecord(rawState))) throw failure('E_GUI_STATE', 'The application state provider returned an invalid state.');
    const state = copySouiJson(rawState || {}, 32768, 'E_GUI_STATE') as SouiGuiNode['state'];
    this.active(entry); // Application state getters may remove or dispose registrations.
    const disabled = (entry.element as Element & { disabled?: boolean }).disabled === true ||
      (typeof entry.element.getAttribute === 'function' && entry.element.getAttribute('aria-disabled') === 'true');
    this.active(entry);
    return { id: entry.id, role: entry.role, name: entry.name, enabled: enabled !== false && !disabled,
      busy: entry.running || busy === true, state, actions: Object.keys(entry.actions).sort() };
  }

  public snapshot(): SouiGuiSnapshot {
    if (this.stopped) throw failure('E_DISPOSED', 'The GUI registry has been disposed.');
    const nodes = Array.from(this.entries.values()).map(entry => this.node(entry)).sort((left, right) => left.id.localeCompare(right.id));
    const result: SouiGuiSnapshot = { protocol: 'soui.gui/1', nodes };
    if (this.stopped) throw failure('E_DISPOSED', 'The GUI registry has been disposed.');
    if (utf8Bytes(JSON.stringify(result)) > 65536) throw failure('E_LIMIT_MESSAGE_SIZE', 'The GUI snapshot exceeds 64 KiB.');
    return result;
  }

  private wait<T>(entry: Entry, timeoutMs: number, begin: (resolve: (value: T) => void, reject: (error: unknown) => void, wait: Wait) => void): Promise<T> {
    if (this.waits.size >= 256) return Promise.reject(failure('E_BUSY', 'The GUI wait capacity is exhausted.'));
    return new Promise<T>((resolve, reject) => {
      let waiting = true;
      const finish = () => {
        waiting = false; clearTimeout(timer); this.waits.delete(pending);
        if (this.waits.size === 0 && this.monitor !== undefined) { clearInterval(this.monitor); this.monitor = undefined; }
      };
      const fail = (error: unknown) => {
        if (!waiting) return;
        finish();
        reject(error instanceof SouiBridgeError ? error : failure('E_GUI_ACTION', error instanceof Error ? error.message : 'The application action failed.'));
      };
      const pending: Wait = { entry, reject: fail };
      const timer = setTimeout(() => fail(failure('E_TIMEOUT', 'The GUI wait timed out; application work was not cancelled.')), timeoutMs);
      this.waits.add(pending);
      if (this.monitor === undefined) this.monitor = setInterval(() => {
        for (const item of Array.from(this.waits)) {
          try { this.active(item.entry); if (item.check) item.check(); }
          catch (error) { item.reject(error instanceof SouiBridgeError ? error : failure('E_GUI_STATE', 'GUI observation failed.')); }
        }
      }, 25);
      const accept = (value: T) => { if (waiting) { finish(); resolve(value); } };
      try { begin(accept, fail, pending); } catch (error) { fail(error); }
    });
  }

  public async perform(request: SouiGuiPerformOptions): Promise<SouiJson> {
    options(request, ['id', 'action', 'input', 'timeoutMs']);
    if (!identifier(request.action)) throw failure('E_INVALID_PARAMS', 'A registered action name is required.');
    const action = request.action;
    const timeout = integerOption(request.timeoutMs, 10000, 60000, 'timeoutMs');
    const entry = this.lookup(request.id);
    if (!own(entry.actions, action)) throw failure('E_GUI_ACTION_NOT_FOUND', 'The action is not registered for this element.');
    const input = copySouiJson(request.input === undefined ? {} : request.input, 32768, 'E_INVALID_PARAMS');
    const state = this.node(entry);
    if (!state.enabled) throw failure('E_GUI_DISABLED', 'The registered element is disabled.');
    if (state.busy) throw failure('E_BUSY', 'The registered element is busy.');
    if (this.waits.size >= 256) throw failure('E_BUSY', 'The GUI wait capacity is exhausted.');
    entry.running = true;
    return this.wait<SouiJson>(entry, timeout, (resolve, reject) => {
      // Deferring through Promise also converts synchronous handler throws into failures.
      Promise.resolve().then(() => {
        this.active(entry);
        return entry.actions[action](input);
      }).then(result => {
        entry.running = false;
        try {
          this.active(entry);
          resolve(copySouiJson(result === undefined ? null : result, 65536, 'E_INVALID_RESPONSE'));
        } catch (error) { reject(error); }
      }, error => { entry.running = false; reject(error); });
    });
  }

  public async waitFor(request: SouiGuiWaitOptions): Promise<SouiGuiNode> {
    options(request, ['id', 'state', 'timeoutMs']);
    const timeout = integerOption(request.timeoutMs, 10000, 60000, 'timeoutMs');
    if (request.state !== undefined && !isRecord(request.state)) throw failure('E_INVALID_PARAMS', 'Expected an explicit application state object.');
    const expected = request.state === undefined ? undefined : copySouiJson(request.state, 32768, 'E_INVALID_PARAMS') as SouiGuiNode['state'];
    const entry = this.lookup(request.id);
    return this.wait<SouiGuiNode>(entry, timeout, (resolve, _reject, pending) => {
      pending.check = () => {
        const node = this.node(entry);
        if (expected === undefined ? node.enabled && !node.busy :
            Object.keys(expected).every(key => own(node.state, key) && equal(node.state[key], expected[key]))) resolve(node);
      };
      pending.check();
    });
  }

  public dispose(): void {
    if (this.stopped) return;
    this.stopped = true;
    for (const entry of Array.from(this.entries.values())) entry.removed = true;
    this.entries.clear();
    for (const pending of Array.from(this.waits)) pending.reject(failure('E_DISPOSED', 'The GUI registry was disposed; application work was not cancelled.'));
  }
}

export function createSouiGui(): SouiGui { return new SouiGui(); }
