import '../../apps/demo/src/legacy-component-runtime';
import '../../apps/react/styles.css';
import '../../apps/react/product.css';
import './benchmark.css';

import React, { useCallback, useEffect, useLayoutEffect, useMemo, useRef, useState } from 'react';
import { render } from 'react-dom';
import { ProductHome, type ProductPage } from '../../apps/react/product-home';
import { ProductTitlebar, type WindowAction } from '../../apps/react/product-shell';
import { AsyncMemoryAdapter, FIXTURE, SCENES, type BenchmarkRow, type SceneId } from './benchmark-contract';

const MOTION_DURATION_MS = 120000;
const MAX_EVENT_RECORDS = 12000;
const MAX_RAF_RECORDS = 16000;

type HostBridge = {
  kind: 'electron';
  writeResult: (result: BenchmarkResult) => Promise<{ ok: boolean }>;
};

type InputRecord = {
  type: string;
  atMs: number;
  target: string;
  key?: string;
  valueLength?: number;
};

type CommitRecord = {
  name: string;
  atMs: number;
  detail?: string;
};

type ActionRecord = {
  name: string;
  startedAtMs: number;
  committedAtMs?: number;
};

type BenchmarkResult = {
  schemaVersion: 1;
  status: 'measured' | 'invalid';
  scene: SceneId;
  hostKind: string;
  fixture: typeof FIXTURE;
  viewport: { width: number; height: number; devicePixelRatio: number };
  timing: { startedAtMs: number; finishedAtMs: number; durationMs: number };
  js: {
    eventLog: InputRecord[];
    commitLog: CommitRecord[];
    actionLog: ActionRecord[];
    rafTimestampsMs: number[];
    rafIntervalsMs: number[];
    resizeEvents: Array<{ atMs: number; width: number; height: number }>;
    scrollEvents: Array<{ atMs: number; top: number }>;
    rowsLoaded: number;
  };
  sceneChecks: {
    ready: boolean;
    completed: boolean;
    motionRequiredMs?: number;
    motionObservedMs?: number;
    rowCount?: number;
    uniqueRowCount?: number;
    imeEvents: number;
  };
  physicalPresentation: {
    status: 'not-collected';
    reason: string;
    rawIntervalsMs: null;
  };
  scope: string;
};

type WindowWithBenchmark = Window & {
  benchmarkHost?: HostBridge;
  __souiNativePostMessage?: unknown;
  __souiBenchmarkReady?: boolean;
  __souiPerformanceResult?: BenchmarkResult;
  __souiBenchmark?: {
    finish: () => void;
    selectScene: (scene: SceneId) => void;
  };
};

type Command = { name: string; nonce: number } | null;

function now(): number {
  return typeof performance !== 'undefined' && typeof performance.now === 'function' ? performance.now() : Date.now();
}

function elementAction(target: EventTarget | null): string {
  let element = target instanceof Element ? target : null;
  while (element) {
    const action = element.getAttribute('data-benchmark-action');
    if (action) return action;
    element = element.parentElement;
  }
  return '';
}

function createTelemetry(scene: SceneId): { scene: SceneId; startedAtMs: number; motionStartedAtMs?: number; events: InputRecord[]; commits: CommitRecord[]; actions: ActionRecord[]; raf: number[]; rafIntervals: number[]; resize: Array<{ atMs: number; width: number; height: number }>; scroll: Array<{ atMs: number; top: number }>; rowsLoaded: number; imeEvents: number } {
  return { scene, startedAtMs: now(), events: [], actions: [], commits: [], raf: [], rafIntervals: [], resize: [], scroll: [], rowsLoaded: 0, imeEvents: 0 };
}

function useBenchmarkTelemetry(scene: SceneId) {
  const telemetry = useRef(createTelemetry(scene));
  const [, redraw] = useState(0);
  const reset = useCallback((nextScene: SceneId) => { telemetry.current = createTelemetry(nextScene); redraw((value) => value + 1); }, []);
  const recordEvent = useCallback((record: InputRecord) => { if (telemetry.current.events.length < MAX_EVENT_RECORDS) telemetry.current.events.push(record); }, []);
  const recordCommit = useCallback((name: string, detail?: string) => { telemetry.current.commits.push({ name, detail, atMs: now() }); redraw((value) => value + 1); }, []);
  const recordAction = useCallback((name: string) => { telemetry.current.actions.push({ name, startedAtMs: now() }); redraw((value) => value + 1); }, []);
  const completeAction = useCallback((name: string) => {
    const action = [...telemetry.current.actions].reverse().find((item) => item.name === name && item.committedAtMs === undefined);
    if (action) action.committedAtMs = now();
    redraw((value) => value + 1);
  }, []);
  const snapshot = useCallback((completed: boolean): BenchmarkResult => {
    const current = telemetry.current;
    const finishedAtMs = now();
    const host = (window as WindowWithBenchmark).benchmarkHost;
    const hostKind = host?.kind ?? (typeof (window as WindowWithBenchmark).__souiNativePostMessage === 'function' ? 'soui-miniblink' : 'unknown');
    const result: BenchmarkResult = {
      schemaVersion: 1,
      status: completed ? 'measured' : 'invalid',
      scene: current.scene,
      hostKind,
      fixture: FIXTURE,
      viewport: { width: window.innerWidth, height: window.innerHeight, devicePixelRatio: window.devicePixelRatio || 1 },
      timing: { startedAtMs: current.startedAtMs, finishedAtMs, durationMs: finishedAtMs - current.startedAtMs },
      js: {
        eventLog: current.events.slice(),
        commitLog: current.commits.slice(),
        actionLog: current.actions.slice(),
        rafTimestampsMs: current.raf.slice(),
        rafIntervalsMs: current.rafIntervals.slice(),
        resizeEvents: current.resize.slice(),
        scrollEvents: current.scroll.slice(),
        rowsLoaded: current.rowsLoaded,
      },
      sceneChecks: { ready: true, completed, imeEvents: current.imeEvents },
      physicalPresentation: { status: 'not-collected', reason: 'PresentMon/GPUView was unavailable during the frozen capability probe; JS rAF and Miniblink paint timestamps are diagnostic only.', rawIntervalsMs: null },
      scope: 'Same local legacy React bundle and deterministic in-memory adapter. Input records are renderer event timestamps; external SendInput/Win32 timestamps are stored beside this result. No raw physical display-present timestamps were available.',
    };
    if (current.scene === 'S02') {
      result.sceneChecks.rowCount = FIXTURE.rowCount;
      result.sceneChecks.uniqueRowCount = current.rowsLoaded;
    }
    if (current.scene === 'S05') {
      result.sceneChecks.motionRequiredMs = MOTION_DURATION_MS;
      result.sceneChecks.motionObservedMs = current.motionStartedAtMs === undefined ? 0 : Math.max(0, finishedAtMs - current.motionStartedAtMs);
    }
    return result;
  }, []);
  return { telemetry, reset, recordEvent, recordCommit, recordAction, completeAction, snapshot };
}

function ProductSurface({ notify, navigate }: { notify: (text: string) => void; navigate: (page: ProductPage) => void }) {
  return <div className="benchmark-product-surface"><ProductHome navigate={navigate} notify={notify} openMedia={() => notify('R 组基准不调用 native 媒体服务。')} /></div>;
}

function S01Surface({ command, recordCommit, completeAction, notify }: { command: Command; recordCommit: (name: string, detail?: string) => void; completeAction: (name: string) => void; notify: (text: string) => void }) {
  const [variant, setVariant] = useState<'A' | 'B'>('A');
  const [expanded, setExpanded] = useState(false);
  useEffect(() => {
    if (!command) return;
    if (command.name === 's01-switch') { setVariant((value) => value === 'A' ? 'B' : 'A'); recordCommit('s01-switch'); completeAction(command.name); }
    if (command.name === 's01-expand') { setExpanded((value) => !value); recordCommit('s01-expand'); completeAction(command.name); }
  }, [command, completeAction, recordCommit]);
  return <div className="benchmark-scene benchmark-s01">
    <div className="benchmark-scene-head"><div><span className="benchmark-kicker">S01 / PRODUCT HOME</span><h1>首页、切换与展开</h1><p>复用生产 ProductTitlebar + ProductHome；右下控制条只负责可重复的外部输入轨迹。</p></div><div className="benchmark-state"><b>{variant}</b><span>{expanded ? 'expanded' : 'collapsed'}</span></div></div>
    <div className="benchmark-s01-control"><button data-benchmark-action="s01-switch" onClick={() => { setVariant((value) => value === 'A' ? 'B' : 'A'); recordCommit('s01-switch'); completeAction('s01-switch'); }}>切换预览 {variant}</button><button data-benchmark-action="s01-expand" onClick={() => { setExpanded((value) => !value); recordCommit('s01-expand'); completeAction('s01-expand'); }}>展开工具组</button><span>variant={variant} · panel={expanded ? 'expanded' : 'collapsed'}</span></div>
    <div className={`benchmark-s01-preview variant-${variant.toLowerCase()}`}><div className="benchmark-preview-copy"><strong>{variant === 'A' ? 'Local workspace' : 'Review workspace'}</strong><span>{variant === 'A' ? '同一资源，第一种布局' : '同一资源，第二种布局'}</span></div>{expanded && <div className="benchmark-expand-grid">{Array.from({ length: 24 }, (_, index) => <button key={index} data-benchmark-action={`s01-expanded-${index}`}>tool-{String(index + 1).padStart(2, '0')}</button>)}</div>}</div>
    <ProductSurface notify={notify} navigate={() => undefined} />
  </div>;
}

function S02Surface({ command, telemetry, recordCommit, completeAction }: { command: Command; telemetry: ReturnType<typeof useBenchmarkTelemetry>['telemetry']; recordCommit: (name: string, detail?: string) => void; completeAction: (name: string) => void }) {
  const adapter = useMemo(() => new AsyncMemoryAdapter(), []);
  const [rows, setRows] = useState<BenchmarkRow[]>([]);
  const [scrollTop, setScrollTop] = useState(0);
  const scrollRef = useRef<HTMLDivElement>(null);
  useEffect(() => {
    let active = true;
    void adapter.loadAll((loaded) => { if (active) telemetry.current.rowsLoaded = loaded; }).then((loaded) => { if (active) { setRows(loaded); telemetry.current.rowsLoaded = loaded.length; recordCommit('s02-data-ready', String(loaded.length)); } });
    return () => { active = false; };
  }, [adapter, recordCommit, telemetry]);
  useEffect(() => {
    if (command?.name !== 's02-reset') return;
    if (scrollRef.current) scrollRef.current.scrollTop = 0;
    setScrollTop(0); recordCommit('s02-reset'); completeAction(command.name);
  }, [command, completeAction, recordCommit]);
  useLayoutEffect(() => { if (rows.length) recordCommit('s02-scroll-commit', String(Math.round(scrollTop))); }, [recordCommit, rows.length, scrollTop]);
  const rowHeight = FIXTURE.rowHeightPx;
  const first = Math.max(0, Math.floor(scrollTop / rowHeight) - 8);
  const visible = rows.slice(first, first + 34);
  return <div className="benchmark-scene benchmark-s02">
    <div className="benchmark-scene-head"><div><span className="benchmark-kicker">S02 / FIXED MEMORY FIXTURE</span><h1>10,000 行滚动</h1><p>异步内存 adapter 分页加载；固定高度虚拟列表，数据与顺序在两端完全相同。</p></div><div className="benchmark-state"><b>{rows.length.toLocaleString()}</b><span>rows loaded</span></div></div>
    <div className="benchmark-list-card"><div className="benchmark-list-meta"><span>fixture={FIXTURE.fixtureId}</span><span>page={FIXTURE.pageSize}</span><span>scrollTop={Math.round(scrollTop)} px</span></div><div ref={scrollRef} className="benchmark-scroll" data-benchmark-action="s02-scroll-surface" onScroll={(event) => { const top = event.currentTarget.scrollTop; setScrollTop(top); telemetry.current.scroll.push({ atMs: now(), top }); }}><div style={{ height: rows.length * rowHeight, position: 'relative' }}>{visible.map((row, index) => <div key={row.id} data-benchmark-row-id={row.id} className="benchmark-row" style={{ top: (first + index) * rowHeight, height: rowHeight }}><b>{row.name}</b><span>{row.group}</span><span>{row.sizeKb} KB</span><em className={`status-${row.status}`}>{row.status}</em></div>)}</div></div></div>
  </div>;
}

function S03Surface({ command, telemetry, recordCommit, completeAction }: { command: Command; telemetry: ReturnType<typeof useBenchmarkTelemetry>['telemetry']; recordCommit: (name: string, detail?: string) => void; completeAction: (name: string) => void }) {
  const inputRef = useRef<HTMLInputElement>(null);
  const [menuOpen, setMenuOpen] = useState(false);
  const [value, setValue] = useState('');
  const [selected, setSelected] = useState('所有类型');
  const [composing, setComposing] = useState(false);
  useEffect(() => {
    if (!command) return;
    if (command.name === 's03-menu') { setMenuOpen((open) => !open); recordCommit('s03-menu'); completeAction(command.name); }
    if (command.name === 's03-select') { setSelected('视频素材'); setMenuOpen(false); recordCommit('s03-select'); completeAction(command.name); }
    if (command.name === 's03-input' || command.name === 's03-ime') { inputRef.current?.focus(); if (command.name === 's03-ime') recordCommit('s03-ime-focus'); completeAction(command.name); }
  }, [command, completeAction, recordCommit]);
  return <div className="benchmark-scene benchmark-s03">
    <div className="benchmark-scene-head"><div><span className="benchmark-kicker">S03 / INPUT + MENU + IME</span><h1>输入延迟分离</h1><p>菜单切换、普通文本和 Unicode 输入分别记录；真实 IME composition 是否可用由能力探针单独判定。</p></div><div className="benchmark-state"><b>{value.length}</b><span>{composing ? 'composing' : selected}</span></div></div>
    <div className="benchmark-input-card"><div className="benchmark-input-toolbar"><button data-benchmark-action="s03-menu" onClick={() => { setMenuOpen((open) => !open); recordCommit('s03-menu'); completeAction('s03-menu'); }}>菜单：{selected}</button>{menuOpen && <div className="benchmark-menu" data-benchmark-action="s03-menu-panel"><button onClick={() => { setSelected('视频素材'); setMenuOpen(false); recordCommit('s03-select'); completeAction('s03-select'); }}>视频素材</button><button onClick={() => { setSelected('音频素材'); setMenuOpen(false); recordCommit('s03-select'); completeAction('s03-select'); }}>音频素材</button></div>}<span className="benchmark-menu-hint">选择后保持焦点路径不变</span></div><label className="benchmark-input-label">搜索固定 fixture<input ref={inputRef} data-benchmark-action="s03-input-field" value={value} onChange={(event) => setValue(event.target.value)} onCompositionStart={() => { setComposing(true); telemetry.current.imeEvents += 1; recordCommit('s03-composition-start'); }} onCompositionEnd={() => { setComposing(false); telemetry.current.imeEvents += 1; recordCommit('s03-composition-end'); }} onKeyDown={(event) => { if (event.key === 'Enter') recordCommit('s03-enter'); }} placeholder="输入文字 / Unicode 注入" /></label><div className="benchmark-input-result">{value ? `匹配 ${value.length * 17} 个候选 · ${value}` : '等待输入事件…'}</div></div>
  </div>;
}

function S04Surface({ command, recordCommit, completeAction }: { command: Command; recordCommit: (name: string, detail?: string) => void; completeAction: (name: string) => void }) {
  const [size, setSize] = useState({ width: window.innerWidth, height: window.innerHeight });
  const [armed, setArmed] = useState(false);
  useEffect(() => {
    const onResize = () => { const next = { width: window.innerWidth, height: window.innerHeight }; setSize(next); recordCommit('s04-resize-commit', `${next.width}x${next.height}`); };
    window.addEventListener('resize', onResize);
    return () => window.removeEventListener('resize', onResize);
  }, [recordCommit]);
  useEffect(() => { if (command?.name === 's04-arm') { setArmed(true); recordCommit('s04-arm'); completeAction(command.name); } }, [command, completeAction, recordCommit]);
  return <div className="benchmark-scene benchmark-s04">
    <div className="benchmark-scene-head"><div><span className="benchmark-kicker">S04 / WINDOW SYSTEM</span><h1>Resize、拖动与窗口移动</h1><p>网页记录 resize/鼠标事件；窗口尺寸和位置由外部 Win32 输入轨迹记录，避免把 DOM 事件当作窗口呈现。</p></div><div className="benchmark-state"><b>{size.width}×{size.height}</b><span>{armed ? 'external trace armed' : 'waiting'}</span></div></div>
    <div className="benchmark-window-card"><div className="benchmark-drag-target" data-benchmark-action="s04-drag-target" onMouseDown={() => recordCommit('s04-drag-pointer-down')}><span className="benchmark-grip" />外部拖动目标 / DOM mouse events</div><div className="benchmark-window-grid"><div><b>client viewport</b><strong>{size.width} × {size.height}</strong></div><div><b>resize events</b><strong>{document.documentElement.getAttribute('data-benchmark-ready') === 'true' ? 'captured' : 'pending'}</strong></div><div><b>window movement</b><strong>external Win32 trace</strong></div></div></div>
  </div>;
}

function S05Surface({ command, telemetry, recordCommit, completeAction }: { command: Command; telemetry: ReturnType<typeof useBenchmarkTelemetry>['telemetry']; recordCommit: (name: string, detail?: string) => void; completeAction: (name: string) => void }) {
  const [running, setRunning] = useState(false);
  const [elapsed, setElapsed] = useState(0);
  const runningRef = useRef(false);
  useEffect(() => {
    if (command?.name !== 's05-start-motion') return;
    if (runningRef.current) return;
    runningRef.current = true; setRunning(true); recordCommit('s05-motion-start'); completeAction(command.name);
    const started = now();
    telemetry.current.motionStartedAtMs = started;
    let previous = started;
    const tick = (timestamp: number) => {
      if (!runningRef.current) return;
      if (telemetry.current.raf.length < MAX_RAF_RECORDS) telemetry.current.raf.push(timestamp);
      if (telemetry.current.rafIntervals.length < MAX_RAF_RECORDS && previous > 0) telemetry.current.rafIntervals.push(timestamp - previous);
      previous = timestamp;
      const nextElapsed = timestamp - started; setElapsed(nextElapsed);
      if (nextElapsed >= MOTION_DURATION_MS) { runningRef.current = false; setRunning(false); recordCommit('s05-motion-complete', String(Math.round(nextElapsed))); return; }
      window.requestAnimationFrame(tick);
    };
    window.requestAnimationFrame(tick);
  }, [command, completeAction, recordCommit, telemetry]);
  useEffect(() => () => { runningRef.current = false; }, []);
  return <div className="benchmark-scene benchmark-s05">
    <div className="benchmark-scene-head"><div><span className="benchmark-kicker">S05 / MOTION DIAGNOSTIC</span><h1>连续动效 120 秒</h1><p>CSS transform/opacity 是被测视觉内容；rAF 只作为 JS 调度诊断，不能替代物理呈现间隔。</p></div><div className="benchmark-state"><b>{Math.floor(elapsed / 1000)}s</b><span>{running ? 'running' : elapsed >= MOTION_DURATION_MS ? 'complete' : 'ready'}</span></div></div>
    <div className={`benchmark-motion-stage ${running ? 'is-running' : ''}`}><div className="benchmark-motion-orbit orbit-a"><i /><b>GPU-safe 2D</b></div><div className="benchmark-motion-orbit orbit-b"><i /><b>opacity track</b></div><div className="benchmark-motion-orbit orbit-c"><i /><b>fixed scene</b></div><div className="benchmark-motion-center">{running ? 'MEASURING' : 'READY'}<small>120,000 ms contract</small></div></div>
  </div>;
}

function BenchmarkBar({ scene, command, selectScene, dispatch, finish, status }: { scene: SceneId; command: Command; selectScene: (scene: SceneId) => void; dispatch: (name: string) => void; finish: () => void; status: string }) {
  const actions: Record<SceneId, string[]> = { S01: ['切换', '展开'], S02: ['回到顶部'], S03: ['菜单', '选择', '输入', 'Unicode'], S04: ['准备窗口'], S05: ['开始 120s'] };
  const sceneIndex = SCENES.findIndex((item) => item.id === scene);
  return <div className="benchmark-bar"><div className="benchmark-scene-tabs">{SCENES.map((item) => <button key={item.id} data-benchmark-action={`select-${item.id}`} className={item.id === scene ? 'active' : ''} onClick={() => selectScene(item.id)}>{item.id}<small>{item.label}</small></button>)}</div><div className="benchmark-actions">{actions[scene].map((label, index) => <button key={label} data-benchmark-action={`dispatch-${scene}-${index}`} onClick={() => dispatch(({ S01: ['s01-switch', 's01-expand'], S02: ['s02-reset'], S03: ['s03-menu', 's03-select', 's03-input', 's03-ime'], S04: ['s04-arm'], S05: ['s05-start-motion'] } as Record<SceneId, string[]>)[scene][index])}>{label}</button>)}<span className="benchmark-command-state">{command?.name ?? 'no action'} · {status}</span></div><button className="benchmark-finish" data-benchmark-action="finish" onClick={finish}>完成场景 <small>{sceneIndex + 1}/5</small></button></div>;
}

function BenchmarkApp() {
  const queryScene = new URLSearchParams(window.location.search).get('scene');
  const initialScene = SCENES.some((item) => item.id === queryScene) ? queryScene as SceneId : 'S01';
  const [scene, setScene] = useState<SceneId>(initialScene);
  const [command, setCommand] = useState<Command>(null);
  const [status, setStatus] = useState('ready');
  const telemetryApi = useBenchmarkTelemetry(scene);
  const { telemetry, reset, recordEvent, recordCommit, recordAction, completeAction, snapshot } = telemetryApi;
  const notify = useCallback((text: string) => setStatus(text), []);
  const selectScene = useCallback((nextScene: SceneId) => {
    // The external runner intentionally clicks the selected tab twice to
    // tolerate first-focus transitions.  Keep that idempotent click from
    // erasing an async fixture that finished between the two clicks.
    if (nextScene !== scene) reset(nextScene);
    setScene(nextScene); setCommand(null); setStatus('scene-selected');
  }, [reset, scene]);
  const dispatch = useCallback((name: string) => { recordAction(name); setCommand({ name, nonce: Date.now() }); setStatus(name); }, [recordAction]);
  const finish = useCallback(() => {
    const complete = scene !== 'S05' || telemetry.current.commits.some((item) => item.name === 's05-motion-complete');
    const result = snapshot(complete);
    const target = window as WindowWithBenchmark;
    target.__souiPerformanceResult = result;
    setStatus(complete ? 'result-written' : 'invalid-motion-duration');
    const host = target.benchmarkHost;
    if (host) void host.writeResult(result).catch((error) => setStatus(`write-failed:${String(error)}`));
  }, [scene, snapshot, telemetry]);
  useEffect(() => {
    const target = window as WindowWithBenchmark;
    target.__souiBenchmarkReady = true;
    document.documentElement.setAttribute('data-benchmark-ready', 'true');
    const eventNames = ['mousedown', 'mouseup', 'click', 'keydown', 'input', 'compositionstart', 'compositionend', 'wheel'];
    const listeners = eventNames.map((type) => {
      const listener = (event: Event) => {
        const keyboard = event as KeyboardEvent;
        const input = event as InputEvent;
        if (type === 'compositionstart' || type === 'compositionend') telemetry.current.imeEvents += 1;
        recordEvent({ type, atMs: now(), target: elementAction(event.target), key: keyboard.key, valueLength: typeof input.data === 'string' ? input.data.length : undefined });
      };
      document.addEventListener(type, listener, type === 'wheel' ? { passive: true } : false);
      return { type, listener };
    });
    const onResize = () => telemetry.current.resize.push({ atMs: now(), width: window.innerWidth, height: window.innerHeight });
    window.addEventListener('resize', onResize);
    return () => { listeners.forEach(({ type, listener }) => document.removeEventListener(type, listener)); window.removeEventListener('resize', onResize); };
  }, [recordEvent, telemetry]);
  useLayoutEffect(() => { recordCommit('scene-ready', scene); }, [recordCommit, scene]);
  useEffect(() => { (window as WindowWithBenchmark).__souiBenchmark = { finish, selectScene }; }, [finish, selectScene]);
  return <><ProductTitlebar page="home" navigate={() => selectScene('S01')} action={(method: WindowAction) => notify(`window-action:${method}`)} notify={notify} /><div className="benchmark-app"><div className="benchmark-host-banner"><span>R GROUP / SOUI + ELECTRON</span><b>{scene}</b><small>legacy resources · fixed fixture · no native service</small></div>{scene === 'S01' && <S01Surface command={command} recordCommit={recordCommit} completeAction={completeAction} notify={notify} />}{scene === 'S02' && <S02Surface command={command} telemetry={telemetry} recordCommit={recordCommit} completeAction={completeAction} />}{scene === 'S03' && <S03Surface command={command} telemetry={telemetry} recordCommit={recordCommit} completeAction={completeAction} />}{scene === 'S04' && <S04Surface command={command} recordCommit={recordCommit} completeAction={completeAction} />}{scene === 'S05' && <S05Surface command={command} telemetry={telemetry} recordCommit={recordCommit} completeAction={completeAction} />}</div><BenchmarkBar scene={scene} command={command} selectScene={selectScene} dispatch={dispatch} finish={finish} status={status} /></>;
}

const root = document.getElementById('benchmark-root');
if (!root) throw new Error('benchmark-root is missing');
render(<BenchmarkApp />, root);
