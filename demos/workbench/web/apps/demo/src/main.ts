import './legacy-component-runtime';
import { hasWorkbenchLocalResources } from '../../../packages/sdk/owned-local-resource-probe';
import { createApp, nextTick, ref, watchEffect } from 'vue';
import App from './App.vue';
import './styles.scss';

import {
  SouiBridgeError,
  SouiNativeClientImpl,
  type SouiNativeEvent,
  type SouiNativePort,
  type SouiSessionLease,
} from '../../../packages/sdk/soui_native_client';

type NativeBootstrapResult = {
  sessionId?: string;
  origin?: string;
  token?: string;
  lease?: SouiSessionLease;
  benchmarkRootToken?: string;
  reactMode?: boolean;
};

type NativeHost = Window & {
  __souiNativePostMessage?: (envelope: string) => string;
  __souiNativeDispatchEvent?: (event: SouiNativeEvent) => void;
};

const host = window as NativeHost;
let probeSessionId = '';

class SynchronousNativePort implements SouiNativePort {
  public dispatchedEvents = 0;
  private readonly listeners = new Set<(event: SouiNativeEvent) => void>();
  private releaseDocument?: () => void;

  public bindDocument(client: SouiNativeClientImpl, dispatch: (event: SouiNativeEvent) => void): void {
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

  public dispose(): void {
    this.listeners.clear();
    const release = this.releaseDocument;
    this.releaseDocument = undefined;
    release?.();
  }

  public request(envelope: string): Promise<string> {
    const postMessage = host.__souiNativePostMessage;
    if (typeof postMessage !== 'function') {
      return Promise.reject(new SouiBridgeError('E_HOST_UNAVAILABLE', 'SOUI/Miniblink host is unavailable.', ''));
    }
    try {
      return Promise.resolve(postMessage(envelope));
    } catch (error) {
      return Promise.reject(error);
    }
  }

  public onEvent(listener: (event: SouiNativeEvent) => void): () => void {
    this.listeners.add(listener);
    return () => this.listeners.delete(listener);
  }

  public dispatch(event: SouiNativeEvent): void {
    if (import.meta.env.MODE === 'performance') this.dispatchedEvents++;
    for (const listener of this.listeners) listener(event);
  }
  public listenerCount(): number { return this.listeners.size; }
}

function parseResponse(raw: string, requestId: string): NativeBootstrapResult {
  let response: { requestId?: string; ok?: boolean; result?: NativeBootstrapResult; error?: { code?: string; message?: string } };
  try {
    response = JSON.parse(raw) as typeof response;
  } catch {
    throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native bootstrap response is not valid JSON.', requestId);
  }
  if (response.requestId !== requestId) {
    throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native bootstrap response correlation id does not match.', requestId);
  }
  if (!response.ok) {
    throw new SouiBridgeError(response.error?.code ?? 'E_HOST_UNAVAILABLE', response.error?.message ?? 'Native bootstrap failed.', requestId);
  }
  return response.result ?? {};
}

function normalizeEvent(value: unknown): SouiNativeEvent | null {
  if (!value || typeof value !== 'object') return null;
  const raw = value as Partial<SouiNativeEvent> & { event?: string; data?: unknown };
  if (typeof raw.sessionId !== 'string' || typeof raw.sequence !== 'number') return null;
  const eventName = raw.eventName ?? raw.event;
  if (typeof eventName !== 'string') return null;
  return {
    sessionId: raw.sessionId,
    taskId: raw.taskId,
    sequence: raw.sequence,
    eventName,
    payload: raw.payload ?? raw.data,
  };
}

function createNativeClient(port: SynchronousNativePort): SouiNativeClientImpl | null {
  if (typeof host.__souiNativePostMessage !== 'function') return null;
  const requestId = 'bridge-bootstrap:1';
  const bootstrapStartedAt = performance.now();
  const raw = host.__souiNativePostMessage(JSON.stringify({
    requestId,
    sessionId: '',
    origin: '',
    token: '',
    method: 'bridge.bootstrap',
    params: { protocol: 'soui.bridge/1' },
  }));
  const bootstrap = parseResponse(raw, requestId);
  (window as Window & { __souiReactMode?: boolean }).__souiReactMode = bootstrap.reactMode === true;
  if (bootstrap.reactMode === true && bootstrap.sessionId) {
    (window as Window & { __souiReactSessionId?: string }).__souiReactSessionId = bootstrap.sessionId;
  }
  if (import.meta.env.MODE === 'performance') {
    (window as Window & { __souiBenchmarkRoot?: string }).__souiBenchmarkRoot = bootstrap.benchmarkRootToken;
    probeSessionId = bootstrap.sessionId ?? '';
  }
  if (!bootstrap.sessionId || !bootstrap.origin || !bootstrap.token) {
    throw new SouiBridgeError('E_INVALID_RESPONSE', 'Native bootstrap did not issue a complete session.', requestId);
  }
  const client = new SouiNativeClientImpl(port, {
    sessionId: bootstrap.sessionId,
    origin: bootstrap.origin,
    token: bootstrap.token,
    lease: bootstrap.lease,
    leaseElapsedMs: bootstrap.lease === undefined ? 0 : performance.now() - bootstrapStartedAt,
  });
  port.bindDocument(client, value => {
    const event = normalizeEvent(value);
    if (event) port.dispatch(event);
  });
  return client;
}

const port = new SynchronousNativePort();
let vueApp: ReturnType<typeof createApp> | null = null;
let nativeClient: SouiNativeClientImpl | null = null;
try {
  nativeClient = createNativeClient(port);
} catch (error) {
  console.warn(error instanceof Error ? error.message : 'Native bootstrap failed.');
}

const reactMode = (window as Window & { __souiReactMode?: boolean }).__souiReactMode === true;
if (reactMode) {
  void import('../../react/main').then(({ mountReactSurface }) => mountReactSurface(nativeClient));
} else {
  vueApp = createApp(App, { nativeClient });
  if(import.meta.env.MODE==='gallery-experiment' && import.meta.env.VITE_TRACE_DOM==='true') {
    void import('./dom-call-trace').then(module=>module.installDomCallTrace());
  }
  if (import.meta.env.MODE === 'gallery-experiment' && import.meta.env.VITE_TRACE_COMPONENT === 'true') {
    const trace = (window as Window & {__souiBenchmarkGalleryCheckpoint?: (name:string,mounted:boolean)=>boolean}).__souiBenchmarkGalleryCheckpoint;
    vueApp.mixin({
      beforeMount() { const name=this.$options.name; if(name) trace?.(`Mount${name}`,false); },
      mounted() { const name=this.$options.name; if(name) trace?.(`Mount${name}`,true); },
      beforeUnmount() { const name=this.$options.name; if(name) trace?.(`Unmount${name}`,false); },
      unmounted() { const name=this.$options.name; if(name) trace?.(`Unmount${name}`,true); },
    });
  }
  vueApp.mount('#app');
}

async function reportRuntimeProbe(): Promise<void> {
  if (!nativeClient) return;
  const eventTarget = document.createElement('button');
  let eventObserved = false;
  eventTarget.addEventListener('click', () => { eventObserved = true; });
  eventTarget.click();
  let errorObserved = false;
  try { JSON.parse('{invalid'); } catch { errorObserved = true; }
  const value = ref(0);
  let observedValue = -1;
  const stop = watchEffect(() => { observedValue = value.value; });
  value.value = 1;
  await nextTick();
  stop();
  await nativeClient.call('bridge.probe.report', {
    script: true,
    json: JSON.parse('{"ok":true}').ok === true,
    event: typeof document.addEventListener === 'function',
    errorHandling: errorObserved,
    localResource: hasWorkbenchLocalResources(location),
    proxy: typeof Proxy === 'function',
    promise: typeof Promise === 'function',
    symbol: typeof Symbol === 'function',
    reflect: typeof Reflect === 'object',
    vueMount: (document.getElementById('app')?.childElementCount ?? 0) > 0,
    reactivity: observedValue === 1,
    domEvent: eventObserved,
    bridge: typeof host.__souiNativePostMessage === 'function',
  });
  document.documentElement.setAttribute('data-soui-native-ready', 'true');
}

if (!reactMode) {
  void reportRuntimeProbe().catch((error) => {
    const message = error instanceof Error ? error.message : 'Runtime capability probe failed.';
    (window as Window & { __souiBootstrapError?: string }).__souiBootstrapError = message;
    console.warn(message);
  });
}

if (import.meta.env.MODE === 'performance' && !reactMode) {
  void (async () => {
    const timings: number[] = [];
    const destination = window as Window & { __souiPerformanceResult?: unknown; __souiTaskLoopProgress?: unknown };
    const cycles: unknown[] = [];
    const pingTimings: number[] = [];
    const loopStarted = performance.now();
    try {
      if (!nativeClient) throw new Error('Native bootstrap is unavailable; diagnostic run cannot pass.');
      if (import.meta.env.VITE_PAGE_RELOAD_CHECK === 'true') {
        const prefix = 'soui-reload-check:';
        if (!window.name.startsWith(prefix)) {
          location.hash = 'gallery-top';
          await new Promise(resolve => setTimeout(resolve, 100));
          await reportRuntimeProbe();
          window.name = prefix + probeSessionId;
          if (import.meta.env.VITE_RECONNECT_UI_CHECK === 'true') {
            document.querySelector<HTMLButtonElement>('.nav-item')!.click();
            await nextTick();
            document.querySelector<HTMLButtonElement>('.preview-actions [aria-controls="studio-inspector"]')!.click();
            const deadline = performance.now() + 5000;
            while (!document.querySelector('.native-reconnect')) {
              if (performance.now() > deadline) throw new Error('Reconnect UI did not load');
              await new Promise(resolve => setTimeout(resolve, 20));
            }
            const button = document.querySelector<HTMLButtonElement>('[data-action="reconnect"]')!;
            if (!button.disabled) throw new Error('Reconnect was enabled without confirmation');
            document.querySelector<HTMLInputElement>('.native-reconnect input')!.click();
            await nextTick();
            if (button.disabled) throw new Error('Confirmed reconnect remained disabled');
            button.click();
          } else location.reload();
          return;
        }
        const previousSession = window.name.slice(prefix.length);
        if (!probeSessionId || previousSession === probeSessionId) throw new Error('Reload did not create a fresh session');
        location.href = 'about:blank';
        await new Promise(resolve => setTimeout(resolve, 100));
        if (location.protocol !== 'file:') throw new Error('Untrusted document navigation was allowed');
        await reportRuntimeProbe();
        destination.__souiPerformanceResult = {status:'measured',browserReload:{previousSession,currentSession:probeSessionId,untrustedNavigationBlocked:true,viaConfirmedUI:import.meta.env.VITE_RECONNECT_UI_CHECK === 'true'},scope:'Browser-initiated reload and real SDK bootstrap; not performance acceptance'};
        window.name = '';
        return;
      }
      // Standard SDK plus the existing parameter-bearing native probe, not an empty RPC.
      for (let sample = 0; sample < 100; sample++) {
        const started = performance.now();
        await reportRuntimeProbe();
        timings.push(performance.now() - started);
      }
      const scanHost = window as Window & { __souiBenchmarkRoot?: string; __souiScanDriver?: (token: string) => Promise<unknown> };
      const highRefresh = import.meta.env.VITE_HIGH_REFRESH === 'true' && nativeClient
        ? await (await import('./high-refresh-probe')).measureHighRefresh(nativeClient) : null;
      if (import.meta.env.VITE_PING_CHECK === 'true') {
        if (!nativeClient) throw new Error('Native client missing');
        for (let sample = 0; sample < 100; sample++) {
          const started = performance.now();
          await nativeClient.call('bridge.benchmark.ping', {});
          pingTimings.push(performance.now() - started);
        }
      }
      const scan = scanHost.__souiBenchmarkRoot && scanHost.__souiScanDriver
        ? await scanHost.__souiScanDriver(scanHost.__souiBenchmarkRoot) : null;
      const duration = Number(import.meta.env.VITE_BENCHMARK_SECONDS ?? '0');
      const retainCycle = (result: unknown) => {
        const writer = (window as Window & { __souiBenchmarkWriteCycle?: (json: string) => boolean }).__souiBenchmarkWriteCycle;
        if (!writer || !writer(JSON.stringify(result))) throw new Error('Raw cycle evidence could not be persisted');
        const summary = result as { files: number; boundaryScroll: string; totalMs: number };
        cycles.push({ files: summary.files, boundaryScroll: summary.boundaryScroll, totalMs: summary.totalMs });
      };
      if (duration > 0) {
        if (duration > 3600 || !scanHost.__souiBenchmarkRoot || !scanHost.__souiScanDriver) throw new Error('Invalid endurance setup');
        retainCycle(scan);
        while (performance.now() - loopStarted < duration * 1000) {
          await new Promise(resolve => setTimeout(resolve, 1000));
          const result = await scanHost.__souiScanDriver(scanHost.__souiBenchmarkRoot);
          retainCycle(result);
          destination.__souiTaskLoopProgress = { cycles: cycles.length, elapsedMs: performance.now() - loopStarted };
        }
      }
      let unmount: { before: number; after: number } | null = null;
      let textInput: unknown = null;
      if (import.meta.env.VITE_TEXT_CHECK === 'true') {
        const textHost = window as Window & { __souiBenchmarkClick?: (x: number, y: number) => number; __souiBenchmarkText?: () => boolean };
        const field = document.querySelector('input:not([readonly])') as HTMLInputElement | null;
        if (!field || !textHost.__souiBenchmarkClick || !textHost.__souiBenchmarkText) throw new Error('Text diagnostic missing');
        const bounds = field.getBoundingClientRect();
        textHost.__souiBenchmarkClick(Math.round(bounds.left + 15), Math.round(bounds.top + bounds.height / 2));
        await new Promise(resolve => setTimeout(resolve, 100));
        if (document.activeElement !== field) throw new Error('Native click did not focus field');
        const changed = new Promise<void>((resolve, reject) => {
          const listener = () => { if (field.value === '中文A') { clearTimeout(timer); field.removeEventListener('input', listener); resolve(); } };
          const timer = setTimeout(() => { field.removeEventListener('input', listener); reject(new Error('Unicode input deadline')); }, 2000);
          field.addEventListener('input', listener);
        });
        textHost.__souiBenchmarkText();
        await changed;
        textInput = { status: 'passed', value: field.value, focused: document.activeElement === field,
          scope: 'WM_CHAR Unicode commit through actual WebView; no IME composition or clipboard claim' };
      }
      const inputSamples: { sentQpc: number; domQpc: number }[] = [];
      if (import.meta.env.VITE_INPUT_CHECK === 'true') {
        const inputHost = window as Window & { __souiBenchmarkClick?: (x: number, y: number) => number; __souiBenchmarkMarkPhase?: (phase: string) => number };
        const button = document.querySelector('.details-toggle');
        if (!button || !inputHost.__souiBenchmarkClick || !inputHost.__souiBenchmarkMarkPhase) throw new Error('Input diagnostics unavailable');
        for (let sample = 0; sample < 20; sample++) {
          const previous = button.getAttribute('aria-expanded');
          const bounds = button.getBoundingClientRect();
          const changed = new Promise<void>((resolve, reject) => {
            const timer = setTimeout(() => { observer.disconnect(); reject(new Error('Input feedback deadline')); }, 2000);
            const observer = new MutationObserver(() => {
              if (button.getAttribute('aria-expanded') !== previous) { clearTimeout(timer); observer.disconnect(); resolve(); }
            });
            observer.observe(button, { attributes: true, attributeFilter: ['aria-expanded'] });
          });
          const sentQpc = inputHost.__souiBenchmarkClick(Math.round(bounds.left + bounds.width / 2), Math.round(bounds.top + bounds.height / 2));
          await changed;
          const domQpc = inputHost.__souiBenchmarkMarkPhase('input');
          inputSamples.push({ sentQpc, domQpc });
          await new Promise(resolve => setTimeout(resolve, 150));
        }
        inputHost.__souiBenchmarkMarkPhase('idle');
      }
      const animationActions: number[] = [];
      if (import.meta.env.VITE_ANIMATION_CHECK === 'true') {
        const element = document.querySelector('.card-heading') as HTMLElement | null;
        if (!element) throw new Error('Animation target missing');
        const original = element.style.transform;
        const markers = window as Window & { __souiBenchmarkMarkPhase?: (phase: string) => void };
        markers.__souiBenchmarkMarkPhase?.('animation');
        try {
          await new Promise<void>(resolve => {
            let step = 0;
            const timer = setInterval(() => {
              animationActions.push(performance.now());
              element.style.transform = `translateX(${Math.sin(step / 10) * 4}px)`;
              if (++step === 180) { clearInterval(timer); resolve(); }
            }, 16);
          });
        } finally {
          element.style.transform = original;
          markers.__souiBenchmarkMarkPhase?.('idle');
        }
      }
      const motionTimes: number[] = [];
      if (import.meta.env.VITE_MOTION_CHECK === 'true') {
        const motionHost = window as Window & { __souiBenchmarkMotion?: (index: number) => boolean; __souiBenchmarkMarkPhase?: (phase: string) => void };
        if (!motionHost.__souiBenchmarkMotion) throw new Error('Motion diagnostic unavailable');
        for (let index = 0; index < 120; index++) {
          motionHost.__souiBenchmarkMarkPhase?.(index < 40 ? 'zoom' : index < 80 ? 'resize' : 'drag');
          const started = performance.now();
          motionHost.__souiBenchmarkMotion(index);
          motionTimes.push(performance.now() - started);
          await new Promise(resolve => setTimeout(resolve, 16));
        }
        motionHost.__souiBenchmarkMotion(120);
        motionHost.__souiBenchmarkMarkPhase?.('idle');
      }
      if (import.meta.env.VITE_UNMOUNT_CHECK === 'true') {
        const before = port.listenerCount();
        vueApp?.unmount();
        await nextTick();
        unmount = { before, after: port.listenerCount() };
      }
      destination.__souiPerformanceResult = { status: 'measured', highRefresh, scan, unmount, motionTimes, animationActions, inputSamples, textInput, dispatchedEvents: port.dispatchedEvents, method: 'bridge.probe.report',
        scope: 'SDK roundtrip plus Vue nextTick and local probe preparation; not empty-call latency', timings, pingTimings, cycles, elapsedMs: performance.now() - loopStarted };
    } catch (error) {
      destination.__souiPerformanceResult = { status: 'failed', error: String(error), timings, cycles, elapsedMs: performance.now() - loopStarted };
    }
  })();
}
