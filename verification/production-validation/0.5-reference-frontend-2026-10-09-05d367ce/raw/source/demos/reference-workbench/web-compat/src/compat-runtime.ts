import ResizeObserverPolyfill from 'resize-observer-polyfill';

// Keep component schedulers usable on the pinned V8/Blink baseline. These
// shims are deliberately small and feature guarded; native implementations
// always win on newer hosts.
if (typeof window.queueMicrotask !== 'function') {
  window.queueMicrotask = (callback) => { Promise.resolve().then(callback); };
}
if (typeof window.requestIdleCallback !== 'function') {
  let idleHandle = 0;
  const idleTimers = new Map<number, number>();
  window.requestIdleCallback = (callback, options) => {
    const handle = ++idleHandle;
    const timeout = Math.max(1, options?.timeout ?? 16);
    const started = Date.now();
    const timer = window.setTimeout(() => {
      idleTimers.delete(handle);
      callback({ didTimeout: Date.now() - started >= timeout, timeRemaining: () => 8 });
    }, Math.min(timeout, 16));
    idleTimers.set(handle, timer);
    return handle;
  };
  window.cancelIdleCallback = (handle) => {
    const timer = idleTimers.get(handle);
    if (timer !== undefined) { window.clearTimeout(timer); idleTimers.delete(handle); }
  };
}

// Local fallback, never a network-loaded compatibility shim.
const omitForExperiment = import.meta.env.MODE === 'gallery-experiment' && import.meta.env.VITE_DISABLE_RESIZE_OBSERVER === 'true';
if (!omitForExperiment && typeof window.ResizeObserver === 'undefined') {
  window.ResizeObserver = ResizeObserverPolyfill;
}

// Miniblink may expose fetch while still rejecting file:// URLs. The DevKit
// is offline-first, so local/data URLs always use this XHR-backed path; native
// fetch remains available for non-local URLs on newer hosts.
const nativeFetch = typeof window.fetch === 'function' ? window.fetch.bind(window) : undefined;
type LocalFetchInit = { method?: string; headers?: Record<string, string>; body?: unknown };
type LocalFetchResponse = { ok: boolean; status: number; url: string; text: () => Promise<string>; json: () => Promise<unknown> };
const localFetch = function (input: string, init?: LocalFetchInit): Promise<LocalFetchResponse> {
    init = init || {};
    if (/^data:/i.test(input)) {
      const comma = input.indexOf(',');
      const metadata = comma >= 0 ? input.slice(0, comma) : input;
      const raw = comma >= 0 ? input.slice(comma + 1) : '';
      let text = raw;
      try {
        text = /;base64/i.test(metadata) ? atob(raw) : decodeURIComponent(raw);
      } catch { /* keep the raw data when decoding is not possible */ }
      return Promise.resolve({ ok: true, status: 200, url: input,
        text: function () { return Promise.resolve(text); },
        json: function () { return Promise.resolve(text).then(function (value) { return JSON.parse(value); }); } });
    }
    return new Promise(function (resolve, reject) {
    const request = new XMLHttpRequest();
    const method = init.method || 'GET';
    request.open(method, input, true);
    Object.keys(init.headers || {}).forEach(function (name) { request.setRequestHeader(name, (init.headers || {})[name]); });
    request.onload = function () {
      const status = request.status === 0 && request.responseText ? 200 : request.status;
      resolve({
        ok: status >= 200 && status < 300,
        status,
        url: input,
        text: function () { return Promise.resolve(request.responseText); },
        json: function () { return Promise.resolve(request.responseText).then(function (value) { return JSON.parse(value); }); },
      });
    };
    request.onerror = function () { reject(new TypeError('Local fetch request failed.')); };
    request.ontimeout = function () { reject(new TypeError('Local fetch request timed out.')); };
    request.send(init.body as any);
    });
};
const isLocalFetchInput = (input: string) => /^(?:data:|file:|\.\.?\/)/i.test(input) || input.indexOf('://') < 0;
if (typeof XMLHttpRequest === 'function') {
  window.fetch = (nativeFetch
    ? function (input: string, init?: { method?: string; headers?: Record<string, string>; body?: unknown }) {
        return isLocalFetchInput(input) ? localFetch(input, init) : nativeFetch(input, init as unknown as RequestInit);
      }
    : localFetch) as unknown as typeof window.fetch;
}

declare global {
  interface Array<T> {
    toSorted?(compareFn?: (a: T, b: T) => number): T[];
    toReversed?(): T[];
    toSpliced?(start: number, deleteCount?: number, ...items: T[]): T[];
    with?(index: number, value: T): T[];
  }
  interface ObjectConstructor {
    groupBy?<T>(items: Iterable<T>, callback: (item: T, index: number) => PropertyKey): Record<PropertyKey, T[]>;
  }
  interface MapConstructor {
    groupBy?<T>(items: Iterable<T>, callback: (item: T, index: number) => unknown): Map<unknown, T[]>;
  }
  interface PromiseConstructor {
    withResolvers?<T>(): { promise: Promise<T>; resolve: (value: T | PromiseLike<T>) => void; reject: (reason?: unknown) => void };
  }
}

// Miniblink's legacy Chromium base can execute the transpiled syntax but does
// not provide every ES2019–ES2024 runtime method. Keep these shims local and
// feature guarded so native implementations win on newer hosts.
const arrayPrototype = Array.prototype as Array<unknown> & {
  at?: (index: number) => unknown;
  flat?: (depth?: number) => unknown[];
  toSorted?: (compareFn?: (a: unknown, b: unknown) => number) => unknown[];
  toReversed?: () => unknown[];
  toSpliced?: (start: number, deleteCount?: number, ...items: unknown[]) => unknown[];
  with?: (index: number, value: unknown) => unknown[];
};
if (!arrayPrototype.at) {
  Object.defineProperty(arrayPrototype, 'at', { configurable: true, value(this: unknown[], index: number) {
    const normalized = index < 0 ? this.length + index : index;
    return normalized >= 0 && normalized < this.length ? this[normalized] : undefined;
  }});
}
if (!arrayPrototype.flat) {
  Object.defineProperty(arrayPrototype, 'flat', { configurable: true, value(this: unknown[], depth = 1) {
    const flatten = (input: unknown[], remaining: number): unknown[] => input.reduce<unknown[]>((result, item) => {
      if (Array.isArray(item) && remaining > 0) return result.concat(flatten(item, remaining - 1));
      result.push(item); return result;
    }, []);
    return flatten(this, Number.isFinite(depth) ? Math.max(0, depth) : 0);
  }});
}
if (!arrayPrototype.toSorted) {
  Object.defineProperty(arrayPrototype, 'toSorted', { configurable: true, value(this: unknown[], compareFn?: (a: unknown, b: unknown) => number) {
    const copy = Array.prototype.slice.call(this) as unknown[];
    return copy.sort(compareFn);
  }});
}
if (!arrayPrototype.toReversed) {
  Object.defineProperty(arrayPrototype, 'toReversed', { configurable: true, value(this: unknown[]) {
    return Array.prototype.slice.call(this).reverse();
  }});
}
if (!arrayPrototype.toSpliced) {
  Object.defineProperty(arrayPrototype, 'toSpliced', { configurable: true, value(this: unknown[], start: number, deleteCount?: number, ...items: unknown[]) {
    const copy = Array.prototype.slice.call(this) as unknown[];
    const normalizedStart = start < 0 ? Math.max(copy.length + start, 0) : Math.min(start, copy.length);
    const count = deleteCount === undefined ? copy.length - normalizedStart : Math.max(0, Math.min(deleteCount, copy.length - normalizedStart));
    copy.splice(normalizedStart, count, ...items);
    return copy;
  }});
}
if (!arrayPrototype.with) {
  Object.defineProperty(arrayPrototype, 'with', { configurable: true, value(this: unknown[], index: number, value: unknown) {
    const copy = Array.prototype.slice.call(this) as unknown[];
    const normalized = index < 0 ? copy.length + index : index;
    if (normalized < 0 || normalized >= copy.length) throw new RangeError('Array index out of range');
    copy[normalized] = value;
    return copy;
  }});
}
if (!String.prototype.replaceAll) {
  Object.defineProperty(String.prototype, 'replaceAll', { configurable: true, value(this: string, search: string | RegExp, replacement: string) {
    if (search instanceof RegExp) return search.global ? this.replace(search, replacement) : this.replace(search, replacement);
    return this.split(search).join(replacement);
  }});
}
if (!Object.groupBy) {
  Object.defineProperty(Object, 'groupBy', { configurable: true, value<T>(items: Iterable<T>, callback: (item: T, index: number) => PropertyKey) {
    const groups: Record<PropertyKey, T[]> = Object.create(null);
    let index = 0;
    for (const item of items) {
      const key = callback(item, index++);
      (groups[key] ||= []).push(item);
    }
    return groups;
  }});
}
if (!(Map as typeof Map & { groupBy?: unknown }).groupBy) {
  Object.defineProperty(Map, 'groupBy', { configurable: true, value<T>(items: Iterable<T>, callback: (item: T, index: number) => unknown) {
    const groups = new Map<unknown, T[]>();
    let index = 0;
    for (const item of items) {
      const key = callback(item, index++);
      const bucket = groups.get(key);
      if (bucket) bucket.push(item); else groups.set(key, [item]);
    }
    return groups;
  }});
}
if (!Object.hasOwn) {
  Object.defineProperty(Object, 'hasOwn', { configurable: true, value(object: object, property: PropertyKey) {
    return Object.prototype.hasOwnProperty.call(object, property);
  }});
}
if (!Promise.any) {
  Object.defineProperty(Promise, 'any', { configurable: true, value<T>(values: Iterable<T | PromiseLike<T>>) {
    return new Promise<T>((resolve, reject) => {
      const errors: unknown[] = []; let pending = 0; let settled = false;
      for (const value of values) {
        pending++;
        Promise.resolve(value).then(result => { if (!settled) { settled = true; resolve(result); } }, error => {
          errors.push(error); if (--pending === 0 && !settled) reject(errors);
        });
      }
      if (pending === 0) reject([]);
    });
  }});
}
if (!Promise.withResolvers) {
  Object.defineProperty(Promise, 'withResolvers', { configurable: true, value<T>() {
    let resolve!: (value: T | PromiseLike<T>) => void;
    let reject!: (reason?: unknown) => void;
    const promise = new Promise<T>((resolvePromise, rejectPromise) => {
      resolve = resolvePromise;
      reject = rejectPromise;
    });
    return { promise, resolve, reject };
  }});
}

if (!Object.fromEntries) {
  Object.defineProperty(Object, 'fromEntries', { configurable: true, value(entries: Iterable<readonly [PropertyKey, unknown]>) {
    const result: Record<PropertyKey, unknown> = {};
    for (const entry of entries) result[entry[0]] = entry[1];
    return result;
  }});
}

const arraySearchPrototype = Array.prototype as Array<unknown> & {
  findLast?: (predicate: (value: unknown, index: number, array: unknown[]) => boolean, thisArg?: unknown) => unknown;
  findLastIndex?: (predicate: (value: unknown, index: number, array: unknown[]) => boolean, thisArg?: unknown) => number;
};
if (!arraySearchPrototype.findLast) {
  Object.defineProperty(arraySearchPrototype, 'findLast', { configurable: true, value(this: unknown[], predicate: (value: unknown, index: number, array: unknown[]) => boolean, thisArg?: unknown) {
    for (let index = this.length - 1; index >= 0; index--) {
      if (predicate.call(thisArg, this[index], index, this)) return this[index];
    }
    return undefined;
  }});
}
if (!arraySearchPrototype.findLastIndex) {
  Object.defineProperty(arraySearchPrototype, 'findLastIndex', { configurable: true, value(this: unknown[], predicate: (value: unknown, index: number, array: unknown[]) => boolean, thisArg?: unknown) {
    for (let index = this.length - 1; index >= 0; index--) {
      if (predicate.call(thisArg, this[index], index, this)) return index;
    }
    return -1;
  }});
}

if (typeof Promise.prototype.finally !== 'function') {
  Object.defineProperty(Promise.prototype, 'finally', { configurable: true, value(this: Promise<unknown>, onFinally?: () => unknown) {
    const constructor = (this.constructor as PromiseConstructor) || Promise;
    const callback = typeof onFinally === 'function' ? onFinally : () => undefined;
    return this.then(
      (value: unknown) => constructor.resolve(callback()).then(() => value),
      (reason: unknown) => constructor.resolve(callback()).then(() => { throw reason; }),
    );
  }});
}

if (typeof window.AbortController === 'undefined') {
  class LocalAbortSignal {
    aborted = false;
    reason: unknown;
    private listeners: Array<() => void> = [];
    addEventListener(type: string, listener: () => void) { if (type === 'abort') this.listeners.push(listener); }
    removeEventListener(type: string, listener: () => void) {
      if (type !== 'abort') return;
      this.listeners = this.listeners.filter(item => item !== listener);
    }
    dispatchEvent(event: { type: string }) {
      if (event.type !== 'abort') return true;
      this.listeners.slice().forEach(listener => listener());
      return true;
    }
  }
  class LocalAbortController {
    readonly signal = new LocalAbortSignal();
    abort(reason?: unknown) {
      if (this.signal.aborted) return;
      this.signal.aborted = true;
      this.signal.reason = reason;
      this.signal.dispatchEvent({ type: 'abort' });
    }
  }
  const legacyWindow = window as unknown as {
    AbortController?: typeof LocalAbortController;
    AbortSignal?: typeof LocalAbortSignal;
  };
  legacyWindow.AbortSignal = LocalAbortSignal;
  legacyWindow.AbortController = LocalAbortController;
}

// Keep CSS feature detection available to modern code on older Miniblink
// builds. The fallback intentionally supports the property/value overload;
// native CSS.supports remains responsible for full condition parsing.
const cssHost = window as Window & { CSS?: (typeof CSS & { supports?: (property: string, value?: string) => boolean }) };
if (!cssHost.CSS) cssHost.CSS = {} as typeof CSS & { supports?: (property: string, value?: string) => boolean };
if (typeof cssHost.CSS.supports !== 'function') {
  const cssObject = cssHost.CSS;
  cssObject.supports = (property: string, value?: string) => {
    if (!value) return false;
    const style = document.createElement('div').style;
    style.setProperty(property, value);
    return style.getPropertyValue(property) !== '' || property in style;
  };
}

// Event-delegation helpers used by modern component libraries. Native
// selector matching wins; the fallback stays local and uses standard DOM
// querySelectorAll semantics.
if (typeof Element !== 'undefined') {
  const elementPrototype = Element.prototype as typeof Element.prototype & {
    matches?: (selector: string) => boolean;
    msMatchesSelector?: (selector: string) => boolean;
    webkitMatchesSelector?: (selector: string) => boolean;
    closest?: (selector: string) => Element | null;
  };
  if (typeof elementPrototype.matches !== 'function') {
    const nativeMatches = elementPrototype.msMatchesSelector || elementPrototype.webkitMatchesSelector;
    Object.defineProperty(elementPrototype, 'matches', { configurable: true, value(this: Element, selector: string) {
      if (nativeMatches) return nativeMatches.call(this, selector);
      const owner = this.ownerDocument;
      if (!owner || typeof owner.querySelectorAll !== 'function') return false;
      const candidates = owner.querySelectorAll(selector);
      for (let index = 0; index < candidates.length; index++) if (candidates[index] === this) return true;
      return false;
    }});
  }
  if (typeof elementPrototype.closest !== 'function') {
    Object.defineProperty(elementPrototype, 'closest', { configurable: true, value(this: Element, selector: string) {
      let current: Element | null = this;
      while (current) { if (current.matches && current.matches(selector)) return current; current = current.parentElement; }
      return null;
    }});
  }
}

const abortSignalConstructor = (window as Window & { AbortSignal?: typeof AbortSignal }).AbortSignal as
  (typeof AbortSignal & { timeout?: (milliseconds: number) => AbortSignal; any?: (signals: AbortSignal[]) => AbortSignal; prototype: AbortSignal & { throwIfAborted?: () => void } }) | undefined;
if (abortSignalConstructor && typeof abortSignalConstructor.timeout !== 'function') {
  abortSignalConstructor.timeout = (milliseconds: number) => {
    const controller = new AbortController();
    window.setTimeout(() => controller.abort(), Math.max(0, milliseconds));
    return controller.signal;
  };
}
if (abortSignalConstructor && typeof abortSignalConstructor.any !== 'function') {
  abortSignalConstructor.any = (signals: AbortSignal[]) => {
    const controller = new AbortController();
    signals.forEach(signal => {
      if (signal.aborted) controller.abort();
      else signal.addEventListener('abort', () => controller.abort());
    });
    return controller.signal;
  };
}
if (abortSignalConstructor && typeof abortSignalConstructor.prototype.throwIfAborted !== 'function') {
  abortSignalConstructor.prototype.throwIfAborted = function (this: AbortSignal) {
    if (!this.aborted) return;
    throw this.reason || new Error('The operation was aborted.');
  };
}

if (typeof window.structuredClone !== 'function') {
  (window as Window & { structuredClone?: <T>(value: T) => T }).structuredClone = function <T>(value: T) {
    return JSON.parse(JSON.stringify(value)) as T;
  };
}

// Chrome 49 exposes URL but not URLSearchParams on every Miniblink build.
// Keep query handling local and deterministic so modern application code can
// use the standard API without loading a remote polyfill.
if (typeof window.URLSearchParams === 'undefined') {
  type QueryPair = [string, string];
  const decodeQueryPart = (value: string) => {
    try { return decodeURIComponent(value.replace(/\+/g, ' ')); }
    catch { return value; }
  };
  const encodeQueryPart = (value: string) => encodeURIComponent(value).replace(/%20/g, '+');
  class LocalURLSearchParams {
    private pairs: QueryPair[] = [];
    constructor(init?: string | Record<string, string> | QueryPair[] | URLSearchParams) {
      if (typeof init === 'string') {
        const source = init.charAt(0) === '?' ? init.slice(1) : init;
        source.split('&').forEach(part => {
          if (!part) return;
          const separator = part.indexOf('=');
          const key = separator < 0 ? part : part.slice(0, separator);
          const value = separator < 0 ? '' : part.slice(separator + 1);
          this.pairs.push([decodeQueryPart(key), decodeQueryPart(value)]);
        });
      } else if (Array.isArray(init)) {
        init.forEach(pair => { if (pair && pair.length > 1) this.append(String(pair[0]), String(pair[1])); });
      } else if (init && typeof init === 'object') {
        Object.keys(init as Record<string, string>).forEach(key => this.append(key, String((init as Record<string, string>)[key])));
      }
    }
    append(name: string, value: string) { this.pairs.push([String(name), String(value)]); }
    delete(name: string) { this.pairs = this.pairs.filter(pair => pair[0] !== String(name)); }
    get(name: string) {
      const pair = this.pairs.filter(item => item[0] === String(name))[0];
      return pair ? pair[1] : null;
    }
    getAll(name: string) { return this.pairs.filter(pair => pair[0] === String(name)).map(pair => pair[1]); }
    has(name: string) { return this.pairs.some(pair => pair[0] === String(name)); }
    set(name: string, value: string) {
      const key = String(name);
      const first = this.pairs.findIndex(pair => pair[0] === key);
      this.delete(key);
      if (first < 0) this.append(key, String(value));
      else this.pairs.splice(Math.min(first, this.pairs.length), 0, [key, String(value)]);
    }
    sort() { this.pairs.sort((left, right) => left[0] < right[0] ? -1 : left[0] > right[0] ? 1 : 0); }
    forEach(callback: (value: string, key: string, parent: LocalURLSearchParams) => void, thisArg?: unknown) {
      this.pairs.slice().forEach(pair => callback.call(thisArg, pair[1], pair[0], this));
    }
    toString() { return this.pairs.map(pair => `${encodeQueryPart(pair[0])}=${encodeQueryPart(pair[1])}`).join('&'); }
  }
  (window as unknown as { URLSearchParams?: typeof LocalURLSearchParams }).URLSearchParams = LocalURLSearchParams;
}

// Chrome 49 exposes the URL constructor but not the modern URL.canParse
// predicate. Keep validation local and avoid throwing from feature detection.
const urlConstructor = window.URL as typeof URL & { canParse?: (input: string | URL, base?: string | URL) => boolean };
if (urlConstructor && typeof urlConstructor.canParse !== 'function') {
  urlConstructor.canParse = (input: string | URL, base?: string | URL) => {
    try { new URL(input, base || document.location.href); return true; }
    catch { return false; }
  };
}

// A few Chrome 49/Miniblink builds omit the UTF-8 codec pair. Keep the
// implementation local and standards-shaped for bridge payloads; native
// TextEncoder/TextDecoder always win on newer runtimes.
if (typeof window.TextEncoder === 'undefined') {
  class LocalTextEncoder {
    readonly encoding = 'utf-8';
    encode(value = '') {
      const bytes: number[] = [];
      for (let index = 0; index < value.length; index++) {
        let codePoint = value.charCodeAt(index);
        if (codePoint >= 0xd800 && codePoint <= 0xdbff && index + 1 < value.length) {
          const low = value.charCodeAt(index + 1);
          if (low >= 0xdc00 && low <= 0xdfff) {
            codePoint = 0x10000 + ((codePoint - 0xd800) << 10) + (low - 0xdc00);
            index++;
          }
        }
        if (codePoint <= 0x7f) bytes.push(codePoint);
        else if (codePoint <= 0x7ff) bytes.push(0xc0 | (codePoint >> 6), 0x80 | (codePoint & 0x3f));
        else if (codePoint <= 0xffff) bytes.push(0xe0 | (codePoint >> 12), 0x80 | ((codePoint >> 6) & 0x3f), 0x80 | (codePoint & 0x3f));
        else bytes.push(0xf0 | (codePoint >> 18), 0x80 | ((codePoint >> 12) & 0x3f), 0x80 | ((codePoint >> 6) & 0x3f), 0x80 | (codePoint & 0x3f));
      }
      return new Uint8Array(bytes);
    }
  }
  (window as Window & { TextEncoder?: typeof LocalTextEncoder }).TextEncoder = LocalTextEncoder;
}
if (typeof window.TextDecoder === 'undefined') {
  class LocalTextDecoder {
    readonly encoding = 'utf-8';
    decode(input?: ArrayBuffer | ArrayBufferView) {
      if (!input) return '';
      const view = input instanceof ArrayBuffer
        ? new Uint8Array(input)
        : new Uint8Array(input.buffer, input.byteOffset, input.byteLength);
      let result = '';
      for (let index = 0; index < view.length;) {
        const first = view[index++];
        let codePoint = 0;
        let continuation = 0;
        if (first <= 0x7f) codePoint = first;
        else if (first >= 0xc2 && first <= 0xdf) { codePoint = first & 0x1f; continuation = 1; }
        else if (first >= 0xe0 && first <= 0xef) { codePoint = first & 0x0f; continuation = 2; }
        else if (first >= 0xf0 && first <= 0xf4) { codePoint = first & 0x07; continuation = 3; }
        else { result += '\ufffd'; continue; }
        let valid = true;
        for (let count = 0; count < continuation; count++) {
          const next = view[index++];
          if ((next & 0xc0) !== 0x80) { valid = false; index -= continuation - count - 1; break; }
          codePoint = (codePoint << 6) | (next & 0x3f);
        }
        if (!valid || (continuation === 2 && codePoint < 0x800) || (continuation === 3 && codePoint < 0x10000) || codePoint > 0x10ffff || (codePoint >= 0xd800 && codePoint <= 0xdfff)) {
          result += '\ufffd';
          continue;
        }
        if (codePoint <= 0xffff) result += String.fromCharCode(codePoint);
        else { const scalar = codePoint - 0x10000; result += String.fromCharCode(0xd800 + (scalar >> 10), 0xdc00 + (scalar & 0x3ff)); }
      }
      return result;
    }
  }
  (window as Window & { TextDecoder?: typeof LocalTextDecoder }).TextDecoder = LocalTextDecoder;
}

if (!String.prototype.matchAll) {
  Object.defineProperty(String.prototype, 'matchAll', { configurable: true, value(this: string, pattern: string | RegExp) {
    const source = pattern instanceof RegExp ? pattern.source : String(pattern);
    const originalFlags = pattern instanceof RegExp ? pattern.flags : '';
    const flags = originalFlags.indexOf('g') >= 0 ? originalFlags : `${originalFlags}g`;
    const expression = new RegExp(source, flags);
    const matches: RegExpExecArray[] = [];
    let match: RegExpExecArray | null;
    while ((match = expression.exec(String(this))) !== null) {
      matches.push(match);
      if (match[0] === '') expression.lastIndex++;
    }
    return matches[Symbol.iterator]();
  }});
}

if (typeof window.IntersectionObserver === 'undefined') {
  type IntersectionEntryLike = { target: Element; isIntersecting: boolean; intersectionRatio: number };
  class LocalIntersectionObserver {
    private readonly callback: (entries: IntersectionEntryLike[]) => void;
    private readonly targets = new Set<Element>();
    private readonly lastStates = new WeakMap<Element, boolean>();
    private scheduledHandle = 0;
    private scheduledAsRaf = false;
    private readonly check = () => {
      const entries: IntersectionEntryLike[] = [];
      for (const target of this.targets) {
        const rect = target.getBoundingClientRect();
        const visible = rect.bottom > 0 && rect.right > 0 && rect.top < window.innerHeight && rect.left < window.innerWidth;
        const previous = this.lastStates.get(target);
        if (previous === undefined || previous !== visible) {
          this.lastStates.set(target, visible);
          entries.push({ target, isIntersecting: visible, intersectionRatio: visible ? 1 : 0 });
        }
      }
      if (entries.length) this.callback(entries);
    };
    private readonly schedule = () => {
      if (this.scheduledHandle) return;
      const flush = () => { this.scheduledHandle = 0; this.scheduledAsRaf = false; this.check(); };
      if (typeof window.requestAnimationFrame === 'function') {
        this.scheduledAsRaf = true;
        this.scheduledHandle = window.requestAnimationFrame(flush);
      } else {
        this.scheduledHandle = window.setTimeout(flush, 16);
      }
    };
    constructor(callback: (entries: IntersectionEntryLike[]) => void) {
      this.callback = callback;
      window.addEventListener('scroll', this.schedule, true);
      window.addEventListener('resize', this.schedule);
    }
    observe(target: Element) { this.targets.add(target); this.lastStates.delete(target); this.schedule(); }
    unobserve(target: Element) { this.targets.delete(target); this.lastStates.delete(target); }
    disconnect() {
      this.targets.clear();
      if (this.scheduledHandle) {
        if (this.scheduledAsRaf && typeof window.cancelAnimationFrame === 'function') window.cancelAnimationFrame(this.scheduledHandle);
        else window.clearTimeout(this.scheduledHandle);
        this.scheduledHandle = 0;
        this.scheduledAsRaf = false;
      }
      window.removeEventListener('scroll', this.schedule, true);
      window.removeEventListener('resize', this.schedule);
    }
    takeRecords() { return []; }
  }
  (window as unknown as { IntersectionObserver?: typeof LocalIntersectionObserver }).IntersectionObserver = LocalIntersectionObserver;
}

if (typeof window.FormData !== 'undefined' && typeof window.FormData.prototype.get !== 'function') {
  const values = new WeakMap<FormData, Record<string, FormDataEntryValue[]>>();
  const originalAppend = window.FormData.prototype.append;
  const remember = (form: FormData, name: string, value: string | Blob, filename?: string) => {
    const entries = values.get(form) || Object.create(null) as Record<string, FormDataEntryValue[]>;
    const storedValue = value instanceof Blob && filename && typeof File !== 'undefined'
      ? new File([value], filename)
      : value as unknown as FormDataEntryValue;
    (entries[name] ||= []).push(storedValue as FormDataEntryValue);
    values.set(form, entries);
  };
  window.FormData.prototype.append = function (this: FormData, name: string, value: string | Blob, filename?: string) {
    (originalAppend as unknown as (name: string, value: string | Blob, filename?: string) => void).call(this, name, value, filename);
    remember(this, name, value, filename);
  };
  Object.defineProperty(window.FormData.prototype, 'get', { configurable: true, value(this: FormData, name: string) {
    const entries = values.get(this);
    const valuesForName = entries && entries[name];
    return valuesForName && valuesForName.length ? valuesForName[valuesForName.length - 1] : null;
  }});
  Object.defineProperty(window.FormData.prototype, 'getAll', { configurable: true, value(this: FormData, name: string) {
    const entries = values.get(this);
    return entries && entries[name] ? entries[name].slice() : [];
  }});
  Object.defineProperty(window.FormData.prototype, 'has', { configurable: true, value(this: FormData, name: string) {
    const entries = values.get(this);
    return !!(entries && entries[name] && entries[name].length);
  }});
  Object.defineProperty(window.FormData.prototype, 'delete', { configurable: true, value(this: FormData, name: string) {
    const entries = values.get(this);
    if (entries) delete entries[name];
  }});
  Object.defineProperty(window.FormData.prototype, 'set', { configurable: true, value(this: FormData, name: string, value: string | Blob, filename?: string) {
    this.delete(name);
    remember(this, name, value, filename);
  }});
}

// Chrome 49 predates several small ES2023/ES2024 helpers that are safe to
// provide without changing the engine parser. Keep these shims local and
// feature-gated so newer Miniblink builds retain their native implementations.
const objectStatics = Object as typeof Object & {
  hasOwn?: (value: object, key: PropertyKey) => boolean;
};
if (typeof objectStatics.hasOwn !== 'function') {
  objectStatics.hasOwn = (value: object, key: PropertyKey) => Object.prototype.hasOwnProperty.call(value, key);
}

const promiseStatics = Promise as unknown as {
  allSettled?: <T>(values: Iterable<T>) => Promise<Array<{ status: 'fulfilled'; value: T } | { status: 'rejected'; reason: unknown }>>;
  try?: <T>(callback: () => T) => Promise<T>;
};
if (typeof promiseStatics.allSettled !== 'function') {
  promiseStatics.allSettled = <T>(values: Iterable<T>) => {
    const settled = Array.prototype.map.call(values, (value: T) =>
    Promise.resolve(value).then(
      (fulfilled: T) => ({ status: 'fulfilled' as const, value: fulfilled }),
      (reason: unknown) => ({ status: 'rejected' as const, reason })
    )) as Array<Promise<{ status: 'fulfilled'; value: T } | { status: 'rejected'; reason: unknown }>>;
    return Promise.all(settled);
  };
}
if (typeof promiseStatics.try !== 'function') {
  promiseStatics.try = <T>(callback: () => T) => Promise.resolve().then(callback);
}

const globalWithAggregateError = window as Window & { AggregateError?: new (errors: unknown[], message?: string) => Error & { errors: unknown[] } };
if (typeof globalWithAggregateError.AggregateError !== 'function') {
  const LocalAggregateError = function (this: Error & { errors: unknown[] }, errors: unknown[], message?: string) {
    this.name = 'AggregateError';
    this.message = message || '';
    this.errors = Array.isArray(errors) ? errors.slice() : [];
  } as unknown as new (errors: unknown[], message?: string) => Error & { errors: unknown[] };
  LocalAggregateError.prototype = Object.create(Error.prototype);
  globalWithAggregateError.AggregateError = LocalAggregateError;
}

const regexpStatics = RegExp as typeof RegExp & { escape?: (value: string) => string };
if (typeof regexpStatics.escape !== 'function') {
  regexpStatics.escape = (value: string) => String(value).replace(/[\\^$.*+?()[\]{}|]/g, '\\$&').replace(/-/g, '\\x2d');
}

type ModernStringPrototype = String & {
  isWellFormed?: () => boolean;
  toWellFormed?: () => string;
};
const modernStringPrototype = String.prototype as ModernStringPrototype;
const isWellFormedString = (value: string) => {
  for (let index = 0; index < value.length; index++) {
    const codeUnit = value.charCodeAt(index);
    if (codeUnit >= 0xd800 && codeUnit <= 0xdbff) {
      const next = value.charCodeAt(index + 1);
      if (next < 0xdc00 || next > 0xdfff) return false;
      index++;
    } else if (codeUnit >= 0xdc00 && codeUnit <= 0xdfff) {
      return false;
    }
  }
  return true;
};
if (typeof modernStringPrototype.isWellFormed !== 'function') {
  Object.defineProperty(String.prototype, 'isWellFormed', { configurable: true, value(this: string) { return isWellFormedString(String(this)); } });
}
if (typeof modernStringPrototype.toWellFormed !== 'function') {
  Object.defineProperty(String.prototype, 'toWellFormed', { configurable: true, value(this: string) {
    const value = String(this);
    let result = '';
    for (let index = 0; index < value.length; index++) {
      const codeUnit = value.charCodeAt(index);
      if (codeUnit >= 0xd800 && codeUnit <= 0xdbff) {
        const next = value.charCodeAt(index + 1);
        if (next >= 0xdc00 && next <= 0xdfff) { result += value.charAt(index) + value.charAt(++index); continue; }
        result += '\ufffd';
      } else if (codeUnit >= 0xdc00 && codeUnit <= 0xdfff) result += '\ufffd';
      else result += value.charAt(index);
    }
    return result;
  }});
}

if (typeof window.Set !== 'undefined') {
  type ModernSetPrototype = Set<unknown> & {
    union?: (other: Iterable<unknown>) => Set<unknown>;
    intersection?: (other: Iterable<unknown>) => Set<unknown>;
    difference?: (other: Iterable<unknown>) => Set<unknown>;
    symmetricDifference?: (other: Iterable<unknown>) => Set<unknown>;
    isSubsetOf?: (other: Iterable<unknown>) => boolean;
    isSupersetOf?: (other: Iterable<unknown>) => boolean;
    isDisjointFrom?: (other: Iterable<unknown>) => boolean;
  };
  const setPrototype = Set.prototype as ModernSetPrototype;
  const has = (other: Iterable<unknown>, value: unknown) => {
    let found = false;
    if (other && typeof (other as any).forEach === 'function') (other as any).forEach((item: unknown) => { if (Object.is(item, value)) found = true; });
    return found;
  };
  if (typeof setPrototype.union !== 'function') Object.defineProperty(Set.prototype, 'union', { configurable: true, value(this: Set<unknown>, other: Iterable<unknown>) { const result = new Set(this); if (other) (other as any).forEach((value: unknown) => result.add(value)); return result; } });
  if (typeof setPrototype.intersection !== 'function') Object.defineProperty(Set.prototype, 'intersection', { configurable: true, value(this: Set<unknown>, other: Iterable<unknown>) { const result = new Set<unknown>(); this.forEach(value => { if (has(other, value)) result.add(value); }); return result; } });
  if (typeof setPrototype.difference !== 'function') Object.defineProperty(Set.prototype, 'difference', { configurable: true, value(this: Set<unknown>, other: Iterable<unknown>) { const result = new Set<unknown>(); this.forEach(value => { if (!has(other, value)) result.add(value); }); return result; } });
  if (typeof setPrototype.symmetricDifference !== 'function') Object.defineProperty(Set.prototype, 'symmetricDifference', { configurable: true, value(this: Set<unknown>, other: Iterable<unknown>) { const result = setPrototype.difference ? setPrototype.difference.call(this, other) : new Set(this); if (other) (other as any).forEach((value: unknown) => { if (result.has(value)) result.delete(value); else result.add(value); }); return result; } });
  if (typeof setPrototype.isSubsetOf !== 'function') Object.defineProperty(Set.prototype, 'isSubsetOf', { configurable: true, value(this: Set<unknown>, other: Iterable<unknown>) { let result = true; this.forEach(value => { if (!has(other, value)) result = false; }); return result; } });
  if (typeof setPrototype.isSupersetOf !== 'function') Object.defineProperty(Set.prototype, 'isSupersetOf', { configurable: true, value(this: Set<unknown>, other: Iterable<unknown>) { let result = true; if (other) (other as any).forEach((value: unknown) => { if (!this.has(value)) result = false; }); return result; } });
  if (typeof setPrototype.isDisjointFrom !== 'function') Object.defineProperty(Set.prototype, 'isDisjointFrom', { configurable: true, value(this: Set<unknown>, other: Iterable<unknown>) { let result = true; this.forEach(value => { if (has(other, value)) result = false; }); return result; } });
}
