import React, { useEffect, useMemo, useState } from 'react';
// Share the pinned Miniblink compatibility layer with the React surface. It
// installs AbortController/CSS/DOM helpers before React Query or PrimeReact
// render, instead of relying on a browser API that Chrome49 does not expose.
import '../demo/src/legacy-component-runtime';
import { render, unmountComponentAtNode } from 'react-dom';
import { lazy, Suspense } from 'react';
import { QueryClient, QueryClientProvider, useQuery } from '@tanstack/react-query';
import { create } from 'zustand';
import { Button } from 'primereact/button';
import { Tag } from 'primereact/tag';
import { ProgressBar } from 'primereact/progressbar';
import type { SouiNativeClientImpl } from '../../packages/sdk/soui_native_client';
import { createReactNativeClient } from './native';
import sample4kMp4 from '../demo/src/media/aurora-4k-sample.mp4';
const samplePoster = 'data:image/svg+xml;base64,PHN2ZyB4bWxucz0iaHR0cDovL3d3dy53My5vcmcvMjAwMC9zdmciIHdpZHRoPSIzMjAiIGhlaWdodD0iMTgwIj48cmVjdCB3aWR0aD0iMzIwIiBoZWlnaHQ9IjE4MCIgZmlsbD0iIzE3M2Y1YyIvPjxyZWN0IHg9IjE4IiB5PSIyNCIgd2lkdGg9IjI4NCIgaGVpZ2h0PSIxMzIiIHJ4PSIxNCIgZmlsbD0iIzNiOTFjZiIvPjxjaXJjbGUgY3g9IjE2MCIgY3k9IjgyIiByPSIzOCIgZmlsbD0iIzJmNzNkZiIvPjx0ZXh0IHg9IjE2MCIgeT0iOTEiIHRleHQtYW5jaG9yPSJtaWRkbGUiIGZvbnQtZmFtaWx5PSJBcmlhbCIgZm9udC1zaXplPSIyOCIgZm9udC13ZWlnaHQ9IjcwMCIgZmlsbD0id2hpdGUiPkhEPC90ZXh0Pjx0ZXh0IHg9IjE2MCIgeT0iMTMwIiB0ZXh0LWFuY2hvcj0ibWlkZGxlIiBmb250LWZhbWlseT0iQXJpYWwiIGZvbnQtc2l6ZT0iMTEiIGxldHRlci1zcGFjaW5nPSIyIiBmaWxsPSIjZGZmNmZmIj5NRURJQSBQUkVWSUVXPC90ZXh0Pjwvc3ZnPg==';
import 'primeicons/primeicons.css';
import 'primereact/resources/primereact.min.css';
import 'primereact/resources/themes/lara-light-blue/theme.css';
import './styles.css';
import { ProductHome, type ProductPage } from './product-home';
import { ProductTitlebar, ProductFeedback, type WindowAction } from './product-shell';
import { DataInteractionLab } from './data-interaction-lab';
import './product.css';
import type { MediaPreset } from './media-workbench';
import { FileInsight } from '../fileinsight/src/FileInsight';
import { isSessionExpired } from '../fileinsight/src/session-recovery';

type Page = ProductPage;
export type RendererStats = { rendererBackend?: string; architecture?: string; activePacingHz?: number; requestedHz?: number; drawIntervalMs?: number; presentCount?: number; sampledAtMs?: number; windowDpi?: number; deviceScaleFactor?: number };
type PreviewResult = { state?: string; supported?: boolean; frameData?: string; decoder?: string; message?: string; progress?: number };
type WorkloadResult = { state?: string; progress?: number; message?: string; metrics?: string };

const initialReactPage = (): Page => {
  if (typeof window === 'undefined') return 'home';
  const requested = (window as Window & { __souiInitialPage?: string }).__souiInitialPage;
  const allowed: Page[] = ['home', 'converter', 'video4k', 'components', 'motion', 'data', 'cad', 'compat', 'fileinsight'];
  if (requested === 'fileinsight-sample' || requested === 'fileinsight-sample-alt') {
    const host = window as Window & { __souiFileInsightAutoSample?: boolean; __souiFileInsightAutoAlternateSample?: boolean };
    host.__souiFileInsightAutoSample = true;
    host.__souiFileInsightAutoAlternateSample = requested === 'fileinsight-sample-alt';
    return 'fileinsight';
  }
  return requested && allowed.indexOf(requested as Page) >= 0 ? requested as Page : 'home';
};

type AppStore = { mediaPreset: MediaPreset; openMedia: (title: string, format: string, quality: number, operation?: string) => void; page: Page; setPage: (page: Page) => void; notice: string; setNotice: (notice: string) => void };
const useAppStore = create<AppStore>((set) => ({ mediaPreset: { title: '视频转换', format: 'MP4', quality: 82 }, openMedia: (title, format, quality, operation) => set({ page: 'converter', mediaPreset: { title, format, quality, operation } }), page: initialReactPage(), setPage: (page) => set(page === 'converter' ? { page, mediaPreset: { title: '视频转换', format: 'MP4', quality: 82 } } : { page }), notice: '', setNotice: (notice) => set({ notice }) }));

class ReactSurfaceErrorBoundary extends React.Component<{ children: React.ReactNode }, { error: Error | null }> {
  state = { error: null as Error | null };
  static getDerivedStateFromError(error: Error) { return { error }; }
  componentDidCatch(error: Error) {
    (window as Window & { __souiReactRenderError?: string }).__souiReactRenderError = error.message;
  }
  render() {
    if (this.state.error) return <main className="rx-page rx-components"><div className="rx-component-head"><div><small>REACT RENDER ERROR</small><h1>React 页面渲染失败</h1><p>{this.state.error.message}</p></div></div></main>;
    return this.props.children;
  }
}

const queryClient = new QueryClient({ defaultOptions: { queries: { retry: false, staleTime: 700 } } });
let nativeClient: SouiNativeClientImpl | null = null;
let reactMounted = false;

// Keep the media workbench route-level lazy because it is a large optional
// task surface. FileInsight stays in the product entry: Miniblink's legacy
// module loader can dispose a dynamically imported React tree under repeated
// native progress traffic on x64, so the native scan/recovery path must remain
// mounted from the first document while preserving the offline Bridge contract.
const LazyMediaWorkbench = lazy(() => import('./media-workbench').then((module) => ({ default: module.MediaWorkbench })));

function useRendererStats() {
  return useQuery<RendererStats>({
    queryKey: ['desktop.rendering.getStats'],
    queryFn: async () => nativeClient ? await nativeClient.call('desktop.rendering.getStats', {}) : { rendererBackend: 'preview-no-host', architecture: 'unknown', activePacingHz: 0, drawIntervalMs: 17 },
    enabled: Boolean(nativeClient), refetchInterval: 1000,
  });
}

async function windowAction(method: WindowAction) {
  if (!nativeClient) { useAppStore.getState().setNotice('窗口操作需要在桌面程序中使用。'); return; }
  try { await nativeClient.call(method, { userGesture: true }); }
  catch (error) { useAppStore.getState().setNotice(error instanceof Error ? error.message : '窗口操作未完成，请重试。'); }
}

function Converter() {
  const { mediaPreset, setPage } = useAppStore();
  return <>
    <Suspense fallback={<main className="rx-page rx-components"><div className="rx-component-head"><div><small>MEDIA WORKBENCH</small><h1>原生媒体工作台加载中…</h1></div></div></main>}>
      <LazyMediaWorkbench key={`${mediaPreset.title}:${mediaPreset.operation || ''}`} client={nativeClient} preset={mediaPreset} home={() => setPage('home')} />
    </Suspense>
    <NativeWorkloads />
    <PublicSdkTask />
  </>;
}

function VideoStudio() {
  const { data: renderer } = useRendererStats();
  const [nativePreview, setNativePreview] = useState<PreviewResult>({ state: 'idle' });
  const [decodeTask, setDecodeTask] = useState<WorkloadResult>({ state: 'idle' });
  const decodeTimer = React.useRef<number | null>(null);
  const decodeTaskRef = React.useRef('');
  const decodeStateRef = React.useRef('idle');
  const decodeLiveRef = React.useRef(true);
  const [ready, setReady] = useState(false);
  const [failed, setFailed] = useState(false);
  const [playing, setPlaying] = useState(false);
  const [current, setCurrent] = useState(0);
  const [duration, setDuration] = useState(1);
  const [speed, setSpeed] = useState(1);
  const videoRef = React.useRef<HTMLVideoElement | null>(null);
  const nativeFrame = nativePreview.frameData && nativePreview.frameData.length > 1000 ? nativePreview.frameData : samplePoster;
  useEffect(() => {
    let live = true;
    let timer: number | null = null;
    let taskId = '';
    const terminal = (state?: string) => ['succeeded', 'failed', 'cancelled'].includes(state ?? '');
    const cancel = (id: string) => { if (id) void nativeClient?.call('task.cancel', { taskId: id }).catch(() => undefined); };
    if (nativeClient) {
      void nativeClient.call('desktop.media.preview.start', { source: 'sample', operationId: `react-4k-studio-${Date.now()}` }).then((started) => {
        if (!live) { cancel(started.taskId); return; }
        taskId = started.taskId;
        timer = window.setInterval(() => {
          void nativeClient?.call('desktop.media.preview.query', { taskId: started.taskId }).then((next) => {
            if (!live) return;
            setNativePreview(next);
            if (terminal(next.state)) {
              if (timer !== null) window.clearInterval(timer);
              timer = null;
              taskId = '';
            }
          }).catch((error) => {
            if (!live) return;
            setNativePreview({ state: 'failed', message: error instanceof Error ? error.message : 'native preview unavailable' });
            if (timer !== null) window.clearInterval(timer);
            timer = null;
            taskId = '';
          });
        }, 150);
      }).catch((error) => { if (live) setNativePreview({ state: 'failed', message: error instanceof Error ? error.message : 'native preview unavailable' }); });
    }
    return () => { live = false; if (timer !== null) window.clearInterval(timer); timer = null; cancel(taskId); taskId = ''; };
  }, []);
  const togglePlayback = () => { const video = videoRef.current; if (!video) return; if (video.paused) { void video.play().then(() => setPlaying(true)).catch(() => undefined); } else { video.pause(); setPlaying(false); } };
  const seek = (value: number) => { const video = videoRef.current; if (!video) return; video.currentTime = value; setCurrent(value); };
  const changeSpeed = (value: number) => { setSpeed(value); if (videoRef.current) videoRef.current.playbackRate = value; };
  const runNativeDecode = async () => { if (!nativeClient || ['running', 'cancel-pending'].includes(decodeStateRef.current)) return; setDecodeTask({ state: 'running', progress: 0, message: 'C++ 4K decode worker starting' }); decodeStateRef.current = 'running'; try { const started = await nativeClient.call('desktop.workload.start', { kind: 'video4k', complexity: 4, operationId: `react-4k-decode-${Date.now()}` }); if (!decodeLiveRef.current) { void nativeClient.call('desktop.workload.cancel', { taskId: started.taskId }).catch(() => undefined); return; } decodeTaskRef.current = started.taskId; decodeTimer.current = window.setInterval(() => { void nativeClient?.call('desktop.workload.query', { taskId: started.taskId }).then((next) => { if (!decodeLiveRef.current) return; decodeStateRef.current = next.state ?? decodeStateRef.current; setDecodeTask(next); if (['succeeded', 'failed', 'cancelled'].includes(next.state ?? '')) { if (decodeTimer.current !== null) window.clearInterval(decodeTimer.current); decodeTimer.current = null; decodeTaskRef.current = ''; } }).catch((error) => { if (!decodeLiveRef.current) return; decodeStateRef.current = 'failed'; decodeTaskRef.current = ''; setDecodeTask({ state: 'failed', message: error instanceof Error ? error.message : 'native 4K query failed' }); if (decodeTimer.current !== null) window.clearInterval(decodeTimer.current); decodeTimer.current = null; }); }, 150); } catch (error) { if (!decodeLiveRef.current) return; decodeTaskRef.current = ''; decodeStateRef.current = 'failed'; setDecodeTask({ state: 'failed', message: error instanceof Error ? error.message : 'native 4K decoder unavailable' }); } };
  const cancelNativeDecode = async () => { const taskId = decodeTaskRef.current; if (!nativeClient || !taskId || !['running', 'accepted', 'queued'].includes(decodeStateRef.current)) return; decodeStateRef.current = 'cancel-pending'; setDecodeTask(previous => ({ ...previous, state: 'cancel-pending', message: '正在请求 native 解码任务取消…' })); try { await nativeClient.call('desktop.workload.cancel', { taskId }); } catch (error) { decodeStateRef.current = 'failed'; setDecodeTask(previous => ({ ...previous, state: 'failed', message: error instanceof Error ? error.message : '取消 native 解码失败' })); } };
  const decodeSummary = (() => { if (!decodeTask.metrics) return decodeTask.message || 'native worker 将报告真实帧数、解码吞吐和 checksum。'; try { const metrics = JSON.parse(decodeTask.metrics) as Record<string, unknown>; return `frames ${metrics.nativeFrames ?? metrics.frames ?? '—'} · throughput ${metrics.throughputMBps ? `${Number(metrics.throughputMBps).toFixed(1)} MB/s` : metrics.decodeFps ? `${Number(metrics.decodeFps).toFixed(1)} fps` : '—'} · ${metrics.decoder ?? 'native decode'}`; } catch { return decodeTask.metrics; } })();
  useEffect(() => () => { decodeLiveRef.current = false; if (decodeTimer.current !== null) window.clearInterval(decodeTimer.current); decodeTimer.current = null; const taskId = decodeTaskRef.current; if (taskId && ['running', 'accepted', 'queued', 'cancel-pending'].includes(decodeStateRef.current)) void nativeClient?.call('desktop.workload.cancel', { taskId }).catch(() => undefined); decodeTaskRef.current = ''; }, []);
  return <main className="rx-page rx-video-studio subpixel-antialiased"><header className="rx-video-head"><div><small>4K VIDEO STUDIO / NATIVE + HTML PIPELINE</small><h1>4K 播放器：浏览器视频与 C++ 解码帧同时在线。</h1><p>本地 MP4 直接进入 Miniblink；若内核无法解码，Media Foundation native frame 立即接管预览，保证产品不会黑屏。</p></div><div className="rx-video-head-tags"><Tag value={renderer?.rendererBackend ?? 'software-safe'} severity="info" /><Tag value={`${renderer?.architecture ?? 'x86/x64'} · ${renderer?.deviceScaleFactor ?? 1}x`} /></div></header><section className="rx-video-layout"><div className="rx-video-stage"><img className={`rx-video-stage-poster ${ready && !failed ? 'hidden' : ''}`} src={nativeFrame} alt="native decoded fallback frame" />{!failed && <video ref={videoRef} className={`rx-video-stage-media ${ready ? 'ready' : ''}`} src={sample4kMp4} muted playsInline preload="metadata" onLoadedMetadata={(event) => { setDuration(event.currentTarget.duration || 1); try { event.currentTarget.currentTime = .35; } catch {} }} onLoadedData={() => undefined} onTimeUpdate={(event) => { const time = event.currentTarget.currentTime || 0; setCurrent(time); if (time > .2) setReady(true); }} onError={() => setFailed(true)} aria-label="4K local player" />}<div className="rx-video-stage-badge"><strong>{ready && !failed ? '4K H.264' : 'MF NATIVE FRAME'}</strong><span>{ready && !failed ? 'HTMLVideoElement / local MP4' : (nativePreview.decoder || 'Media Foundation / RGB32 fallback')}</span></div><div className="rx-video-controls"><button onClick={togglePlayback}>{playing ? '暂停' : '播放'}</button><input type="range" min="0" max={duration} step="0.01" value={current} onChange={(event) => seek(Number(event.target.value))} aria-label="视频时间轴" /><span>{current.toFixed(1)}s / {duration.toFixed(1)}s</span><select value={speed} onChange={(event) => changeSpeed(Number(event.target.value))} aria-label="播放速度"><option value="0.5">0.5×</option><option value="1">1×</option><option value="1.5">1.5×</option><option value="2">2×</option></select></div></div><aside className="rx-video-inspector"><small>PLAYER INSPECTOR</small><h2>同一套 Bridge，三条降级路径。</h2><dl><div><dt>source</dt><dd>aurora-4k-sample.mp4</dd></div><div><dt>resolution</dt><dd>3840 × 2160</dd></div><div><dt>frontend</dt><dd>{ready && !failed ? 'HTML video' : nativePreview.decoder || 'native PNG frame'}</dd></div><div><dt>renderer</dt><dd>{renderer?.rendererBackend ?? 'software-safe'}</dd></div><div><dt>draw budget</dt><dd>{renderer?.drawIntervalMs ?? 17}ms</dd></div></dl><div className="rx-video-capabilities"><Tag value="offline" severity="success" /><Tag value="CPU-safe" severity="warning" /><Tag value="native fallback" severity="info" /></div><button className="rx-video-native-button" onClick={() => void runNativeDecode()} disabled={['running', 'cancel-pending'].includes(decodeStateRef.current)}>{['running', 'cancel-pending'].includes(decodeStateRef.current) ? `C++ 解码中 ${decodeTask.progress ?? 0}%` : '运行 native 4K decode'}</button>{['running', 'cancel-pending'].includes(decodeStateRef.current) && <button className="rx-video-native-button" onClick={() => void cancelNativeDecode()} disabled={decodeStateRef.current === 'cancel-pending'}>取消 native 解码</button>}<p>{decodeSummary}</p></aside></section></main>;
}

const workloadCards = [
  { kind: 'video4k', title: '4K 视频播放器', eyebrow: 'MEDIA', detail: '有界 BGRA 帧管线、时间轴和 GPU/CPU-safe 降级。', complexity: 2 },
  { kind: 'cad', title: 'CAD 编辑器', eyebrow: 'SCENE', detail: '原生场景变换、脏区和可扩展几何 adapter 边界。', complexity: 100000 },
  { kind: 'zip', title: 'ZIP 解压工具', eyebrow: 'TASKS', detail: 'zlib DEFLATE、CRC 校验、进度合并和取消确认。', complexity: 32 },
];

function NativeWorkloads() {
  const { data: capabilities } = useQuery<{ note?: string; zipDeflate?: boolean; video4kNativeFrame?: boolean }>({ queryKey: ['desktop.workload.getCapabilities'], queryFn: async () => nativeClient ? await nativeClient.call('desktop.workload.getCapabilities', {}) : { note: '预览模式 · 未连接 native worker', zipDeflate: false, video4kNativeFrame: false }, enabled: Boolean(nativeClient), staleTime: 10_000 });
  const [selected, setSelected] = useState('zip');
  const [task, setTask] = useState<WorkloadResult>({ state: 'idle' });
  const timerRef = React.useRef<number | null>(null);
  const activeTaskRef = React.useRef('');
  const activeStateRef = React.useRef('idle');
  const liveRef = React.useRef(true);
  useEffect(() => () => { liveRef.current = false; if (timerRef.current !== null) window.clearInterval(timerRef.current); }, []);
  const run = async (kind: string, complexity: number) => {
    if (!nativeClient || task.state === 'running') return;
    setSelected(kind); setTask({ state: 'running', progress: 0, message: '原生 worker 启动中' });
    activeStateRef.current = 'running';
    try {
      const started = await nativeClient.call('desktop.workload.start', { kind, complexity, operationId: `react-workload-${kind}-${Date.now()}` });
      if (!liveRef.current) { void nativeClient.call('desktop.workload.cancel', { taskId: started.taskId }).catch(() => undefined); return; }
      activeTaskRef.current = started.taskId;
      if (timerRef.current !== null) window.clearInterval(timerRef.current);
       timerRef.current = window.setInterval(() => { void nativeClient?.call('desktop.workload.query', { taskId: started.taskId }).then((next) => { if (!liveRef.current) return; activeStateRef.current = next.state ?? activeStateRef.current; setTask(next); if (['succeeded', 'failed', 'cancelled'].includes(next.state ?? '')) { if (timerRef.current !== null) window.clearInterval(timerRef.current); timerRef.current = null; activeTaskRef.current = ''; } }).catch((error) => { if (!liveRef.current) return; setTask({ state: 'failed', message: error instanceof Error ? error.message : 'native workload query failed' }); if (timerRef.current !== null) window.clearInterval(timerRef.current); timerRef.current = null; }); }, 140);
    } catch (error) { if (!liveRef.current) return; activeTaskRef.current = ''; activeStateRef.current = 'failed'; setTask({ state: 'failed', message: error instanceof Error ? error.message : 'native workload unavailable' }); }
  };
  useEffect(() => () => {
    if (timerRef.current !== null) window.clearInterval(timerRef.current);
    timerRef.current = null;
    const taskId = activeTaskRef.current;
    if (taskId && ['running', 'accepted', 'queued', 'cancel-pending'].includes(activeStateRef.current)) {
      void nativeClient?.call('desktop.workload.cancel', { taskId }).catch(() => undefined);
    }
    activeTaskRef.current = '';
  }, []);
  const cancel = async () => {
    const taskId = activeTaskRef.current;
    if (!nativeClient || !taskId || !['running', 'accepted', 'queued'].includes(activeStateRef.current)) return;
    activeStateRef.current = 'cancel-pending';
    setTask(previous => ({ ...previous, state: 'cancel-pending', message: '正在请求 native worker 取消…' }));
    try { await nativeClient.call('desktop.workload.cancel', { taskId }); }
    catch (error) { activeStateRef.current = 'failed'; setTask(previous => ({ ...previous, state: 'failed', message: error instanceof Error ? error.message : '取消任务失败' })); }
  };
  return <section className="rx-workloads"><header><div><small>NATIVE WORKLOAD LAB</small><h2>让重客户端产品也能落地</h2></div><span>{capabilities?.video4kNativeFrame ? '4K MF frame online · ' : ''}{capabilities?.zipDeflate ? 'zlib DEFLATE online' : 'native adapter boundary'}</span></header><div className="rx-workload-grid">{workloadCards.map((card) => <button key={card.kind} className={selected === card.kind ? 'selected' : ''} disabled={task.state === 'running' || task.state === 'cancel-pending'} onClick={() => void run(card.kind, card.complexity)}><i>{card.kind === 'video4k' ? '◉' : card.kind === 'cad' ? '⌁' : '▣'}</i><span><small>{card.eyebrow}</small><strong>{card.title}</strong><em>{card.detail}</em></span><b>{selected === card.kind && ['running', 'cancel-pending'].includes(task.state ?? '') ? `${task.progress ?? 0}%` : 'RUN'}</b></button>)}</div><div className="rx-workload-result"><span>{task.message || capabilities?.note || 'Bridge 只传句柄、进度和有界 metrics JSON'}</span>{task.metrics && <code>{task.metrics}</code>}{['running', 'cancel-pending'].includes(task.state ?? '') && <button type="button" onClick={() => void cancel()} disabled={task.state === 'cancel-pending'}>取消</button>}</div></section>;
}

function PublicSdkTask({ interaction = false }: { interaction?: boolean }) {
  const [task, setTask] = useState<WorkloadResult>({ state: 'idle' });
  const [observations, setObservations] = useState({ events: 0, queries: 0 });
  const [watchError, setWatchError] = useState('');
  const timer = React.useRef<number | null>(null);
  const queryingId = React.useRef('');
  const activeId = React.useRef('');
  const starting = React.useRef(false);
  const activeState = React.useRef('idle');
  const subscription = React.useRef<ReturnType<SouiNativeClientImpl['subscribe']> | null>(null);
  const live = React.useRef(true);
  const stopWatching = () => {
    if (timer.current !== null) window.clearInterval(timer.current);
    timer.current = null;
    if (subscription.current) void subscription.current.unsubscribe().catch(() => undefined);
    subscription.current = null;
  };
  useEffect(() => () => {
    live.current = false;
    stopWatching();
    if (activeId.current && ['queued', 'running', 'cancel-pending'].includes(activeState.current))
      void nativeClient?.call('task.cancel', { taskId: activeId.current }).catch(() => undefined);
  }, []);
  const refresh = async (taskId: string) => {
    if (!nativeClient || !live.current || activeId.current !== taskId || queryingId.current === taskId) return;
    queryingId.current = taskId;
    try {
      const next = await nativeClient.call('task.query', { taskId });
      if (!live.current || activeId.current !== taskId) return;
      activeState.current = next.state;
      setObservations(previous => ({ ...previous, queries: previous.queries + 1 }));
      setTask({ state: next.state, progress: next.progress, message: next.message });
      if (['succeeded', 'failed', 'cancelled', 'partial'].includes(next.state)) {
        stopWatching();
        activeId.current = '';
      }
    } catch (error) {
      if (!live.current || activeId.current !== taskId) return;
      stopWatching();
      void nativeClient.call('task.cancel', { taskId }).catch(() => undefined);
      activeId.current = '';
      activeState.current = 'failed';
      setTask({ state: 'failed', message: error instanceof Error ? error.message : 'SDK 任务查询失败' });
    } finally {
      if (queryingId.current === taskId) queryingId.current = '';
    }
  };
  const start = async (continuous = false) => {
    if (!nativeClient || activeId.current || starting.current) return;
    starting.current = true;
    setObservations({ events: 0, queries: 0 });
    setWatchError('');
    setTask({ state: 'queued', progress: 0, message: 'SDK 任务正在入队' });
    try {
      const started = await nativeClient.call('task.start', {
        kind: continuous ? 'demo.interaction' : 'demo.count',
        operationId: `demo-${continuous ? 'interaction' : 'count'}-${Date.now()}`,
        input: continuous ? { durationMs: 30000 } : { steps: 200 },
      });
      starting.current = false;
      if (!live.current) { void nativeClient.call('task.cancel', { taskId: started.taskId }).catch(() => undefined); return; }
      activeId.current = started.taskId;
      activeState.current = 'queued';
      subscription.current = nativeClient.subscribe(started.taskId, 'task.progress', () => {
        if (!live.current || activeId.current !== started.taskId) return;
        setObservations(previous => ({ ...previous, events: previous.events + 1 }));
        void refresh(started.taskId);
      });
      void subscription.current.ready.catch((error) => {
        if (live.current && activeId.current === started.taskId)
          setWatchError(error instanceof Error ? error.message : '进度订阅失败');
      });
      timer.current = window.setInterval(() => { void refresh(started.taskId); }, 150);
      void refresh(started.taskId);
    } catch (error) {
      starting.current = false;
      if (!live.current) return;
      activeState.current = 'failed';
      setTask({ state: 'failed', message: error instanceof Error ? error.message : 'SDK 任务启动失败' });
    }
  };
  const cancel = async () => {
    const taskId = activeId.current;
    if (!nativeClient || !taskId) return;
    try {
      await nativeClient.call('task.cancel', { taskId });
      if (!live.current || activeId.current !== taskId) return;
      activeState.current = 'cancel-pending';
      setTask(previous => ({ ...previous, state: 'cancel-pending' }));
    } catch (error) {
      if (!live.current || activeId.current !== taskId) return;
      setTask(previous => ({ ...previous, message: error instanceof Error ? error.message : 'SDK 任务取消失败' }));
    }
  };
  return <section className="rx-workloads" data-task-state={task.state} data-task-progress={task.progress ?? 0} data-task-events={observations.events} data-task-queries={observations.queries}>
    <header><div><small>PUBLIC SDK TASK</small><h2>公开任务接口验证</h2></div><span>task.start → progress → query / cancel</span></header>
    {interaction && <p>启动 30 秒原生计时任务后，可同时滚动列表、输入中文和调整窗口；任务支持随时取消。此任务用于交互并发观察，不代表高 CPU 压力测试。</p>}
    <div className="rx-workload-result"><span>{task.message || '从 Demo 页面启动 SDK 原生有界任务'}</span><code>{task.state} · {task.progress ?? 0}%</code>
      <button type="button" onClick={() => void start()} disabled={!nativeClient || starting.current || Boolean(activeId.current)}>快速任务</button>
      {interaction && <button type="button" onClick={() => void start(true)} disabled={!nativeClient || starting.current || Boolean(activeId.current)}>运行 30 秒任务</button>}
      {activeId.current && <button type="button" onClick={() => void cancel()} disabled={task.state === 'cancel-pending'}>取消</button>}
      <span>进度事件 {observations.events} · 状态确认 {observations.queries}</span>
      {watchError && <span role="alert">进度订阅失败：{watchError}</span>}
    </div>
  </section>;
}

const LazyComponentsPage = lazy(() => import('./components-page').then((module) => ({ default: module.ComponentsPage })));

function MotionLab() {
  const { data } = useRendererStats();
  const [burst, setBurst] = useState(false);
  const [fps, setFps] = useState(0);
  const [nativeFps, setNativeFps] = useState(0);
  const [reduced, setReduced] = useState(false);
  const [targetRate, setTargetRate] = useState(0);
  const presentSampleRef = React.useRef<{ count: number; at: number } | null>(null);
  const backend = data?.rendererBackend ?? (nativeClient ? 'gpu-auto-pending' : 'preview-no-host');
  const gpu = backend.indexOf('gpu') === 0;
  useEffect(() => {
    try { setReduced(Boolean(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches)); } catch { setReduced(false); }
    let raf = 0; let frames = 0; let started = performance.now();
    const sample = (now: number) => {
      frames += 1;
      if (now - started >= 1000) { setFps(Math.round(frames * 1000 / (now - started))); frames = 0; started = now; }
      raf = window.requestAnimationFrame(sample);
    };
    raf = window.requestAnimationFrame(sample);
    return () => window.cancelAnimationFrame(raf);
  }, []);
  useEffect(() => {
    // Prefer the native compositor's present counter whenever the host
    // publishes it. RAF callbacks can run faster than the monitor and would
    // otherwise make a 60 Hz window look like a 100+ FPS surface.
    const count = Number(data?.presentCount ?? 0);
    const at = Number(data?.sampledAtMs ?? 0);
    if (!Number.isFinite(count) || !Number.isFinite(at) || count <= 0 || at <= 0) return;
    const previous = presentSampleRef.current;
    presentSampleRef.current = { count, at };
    if (!previous || at <= previous.at || count < previous.count) return;
    const elapsedSeconds = (at - previous.at) / 1000;
    if (elapsedSeconds <= 0) return;
    const measured = Math.max(0, Math.min(240, Math.round((count - previous.count) / elapsedSeconds)));
    const activeHz = Number(data?.activePacingHz ?? 0);
    setNativeFps(activeHz > 0 ? Math.min(measured, activeHz) : measured);
  }, [data]);
  useEffect(() => { if (!burst) return; const timer = window.setTimeout(() => setBurst(false), 950); return () => window.clearTimeout(timer); }, [burst]);
  const stageClass = `rx-motion-stage ${gpu ? 'gpu' : 'cpu'} ${burst ? 'burst' : ''} ${reduced ? 'reduced' : ''}`;
  const chooseRate = async (rate: number) => { setTargetRate(rate); await nativeClient?.call('desktop.rendering.setTargetRate', { targetHz: rate }).catch(() => undefined); };
  const activeHz = Number(data?.activePacingHz ?? 0);
  const fallbackFps = activeHz > 0 ? Math.min(fps || activeHz, activeHz) : fps;
  const displayedFps = nativeFps > 0 ? nativeFps : fallbackFps || 60;
  const fpsLabel = nativeFps > 0 ? 'NATIVE PRESENT FPS' : 'ACTIVE PACING HZ';
  return <main className="rx-page rx-motion subpixel-antialiased"><header className="rx-motion-head"><div><small>MOTION LAB / RENDER PIPELINE</small><h1>让每一次变化，都有真实的重量。</h1><p>同一套 React 组件在 Miniblink GPU direct、GPU readback 和 CPU-safe 三种策略间自动切换。</p></div><button className="rx-motion-trigger" onClick={() => setBurst(true)}>{burst ? '能量脉冲进行中' : '触发能量脉冲'}</button></header><section className="rx-motion-layout"><div className={stageClass} aria-label="GPU and CPU safe motion preview"><div className="rx-motion-grid" aria-hidden="true" /><div className="rx-motion-ring ring-a" aria-hidden="true" /><div className="rx-motion-ring ring-b" aria-hidden="true" /><div className="rx-motion-orbit orbit-a" aria-hidden="true"><i /></div><div className="rx-motion-orbit orbit-b" aria-hidden="true"><i /></div><div className="rx-motion-scan" aria-hidden="true" /><div className="rx-motion-core" aria-hidden="true"><strong>SOUI</strong><span>{gpu ? 'GPU DIRECT' : 'CPU SAFE'}</span></div><div className="rx-motion-trail" aria-hidden="true"><i /><i /><i /></div><b className="rx-motion-label">{gpu ? 'GPU DIRECT / TRANSFORM SAFE' : 'CPU SAFE / DIRTY RECT'}</b></div><aside className="rx-motion-panel"><small>FRAME METER</small><div className="rx-motion-fps"><strong>{displayedFps}</strong><span>{fpsLabel}</span></div><dl><div><dt>backend</dt><dd>{backend}</dd></div><div><dt>present</dt><dd>{data?.presentCount ?? 0}</dd></div><div><dt>draw budget</dt><dd>{data?.drawIntervalMs ?? 17}ms</dd></div><div><dt>active</dt><dd>{data?.activePacingHz ?? 60}Hz</dd></div></dl><div className="rx-motion-rate"><span>target rate</span><div>{[60, 75, 100, 120, 144, 165, 240].map((rate) => <button key={rate} className={(targetRate || data?.requestedHz || 60) === rate ? 'active' : ''} onClick={() => void chooseRate(rate)}>{rate}</button>)}</div></div><div className="rx-motion-tags"><span>transform</span><span>opacity</span><span>{reduced ? 'reduced-motion' : gpu ? 'composited' : 'dirty-region'}</span></div><button className="rx-motion-toggle" onClick={() => setReduced((value) => !value)}>{reduced ? '恢复动效' : '降低动效'}</button></aside></section></main>;
}

type CadTask = { state?: string; progress?: number; message?: string; metrics?: string };
const cadEntities = Array.from({ length: 144 }, (_, index) => ({
  id: index + 1, x: 50 + (index % 12) * 82, y: 55 + Math.floor(index / 12) * 54,
  kind: index % 3, size: 10 + (index % 4) * 3,
}));

function CadStudio() {
  const { data: renderer } = useRendererStats();
  const [zoom, setZoom] = useState(1);
  const [offset, setOffset] = useState({ x: 0, y: 0 });
  const [selected, setSelected] = useState(42);
  const [task, setTask] = useState<CadTask>({ state: 'idle' });
  const timerRef = React.useRef<number | null>(null);
  const taskIdRef = React.useRef('');
  const taskStateRef = React.useRef('idle');
  const liveRef = React.useRef(true);
  const dragRef = React.useRef<{ x: number; y: number; ox: number; oy: number } | null>(null);
  useEffect(() => () => { liveRef.current = false; if (timerRef.current !== null) window.clearInterval(timerRef.current); timerRef.current = null; const taskId = taskIdRef.current; if (taskId && ['running', 'accepted', 'queued', 'cancel-pending'].includes(taskStateRef.current)) void nativeClient?.call('desktop.workload.cancel', { taskId }).catch(() => undefined); taskIdRef.current = ''; }, []);
  const visible = Math.min(cadEntities.length, Math.max(18, Math.round(cadEntities.length * Math.min(1, .55 + zoom * .45))));
  const lod = zoom > 1.35 ? 'LOD-0 / detail' : zoom > .8 ? 'LOD-1 / edges' : 'LOD-2 / clusters';
  const runNative = async () => {
    if (!nativeClient || ['running', 'cancel-pending'].includes(taskStateRef.current)) return;
    setTask({ state: 'running', progress: 0, message: 'native scene transform starting' });
    taskStateRef.current = 'running';
    try {
      const started = await nativeClient.call('desktop.workload.start', { kind: 'cad', complexity: 100000, operationId: `react-cad-${Date.now()}` });
      if (!liveRef.current) { void nativeClient.call('desktop.workload.cancel', { taskId: started.taskId }).catch(() => undefined); return; }
      taskIdRef.current = started.taskId;
      timerRef.current = window.setInterval(() => { void nativeClient?.call('desktop.workload.query', { taskId: started.taskId }).then((next) => { if (!liveRef.current) return; taskStateRef.current = next.state ?? taskStateRef.current; setTask(next); if (['succeeded', 'failed', 'cancelled'].includes(next.state ?? '')) { if (timerRef.current !== null) window.clearInterval(timerRef.current); timerRef.current = null; taskIdRef.current = ''; } }).catch((error) => { if (!liveRef.current) return; taskStateRef.current = 'failed'; taskIdRef.current = ''; setTask({ state: 'failed', message: error instanceof Error ? error.message : 'native CAD query failed' }); if (timerRef.current !== null) window.clearInterval(timerRef.current); timerRef.current = null; }); }, 150);
    } catch (error) { if (!liveRef.current) return; taskStateRef.current = 'failed'; taskIdRef.current = ''; setTask({ state: 'failed', message: error instanceof Error ? error.message : 'native CAD worker unavailable' }); }
  };
  const cancelNative = async () => { const taskId = taskIdRef.current; if (!nativeClient || !taskId || !['running', 'accepted', 'queued'].includes(taskStateRef.current)) return; taskStateRef.current = 'cancel-pending'; setTask(previous => ({ ...previous, state: 'cancel-pending', message: '正在请求 native 场景任务取消…' })); try { await nativeClient.call('desktop.workload.cancel', { taskId }); } catch (error) { taskStateRef.current = 'failed'; setTask(previous => ({ ...previous, state: 'failed', message: error instanceof Error ? error.message : '取消 native 场景任务失败' })); } };
  const beginPan = (event: React.MouseEvent<SVGSVGElement>) => { if (event.button === 0) dragRef.current = { x: event.clientX, y: event.clientY, ox: offset.x, oy: offset.y }; };
  const movePan = (event: React.MouseEvent<SVGSVGElement>) => { const drag = dragRef.current; if (!drag) return; setOffset({ x: drag.ox + (event.clientX - drag.x) / 1.2, y: drag.oy + (event.clientY - drag.y) / 1.2 }); };
  const endPan = () => { dragRef.current = null; };
  return <main className="rx-page rx-cad subpixel-antialiased"><header className="rx-cad-head"><div><small>CAD STUDIO / NATIVE SCENE PIPELINE</small><h1>几何、视口与原生 worker 同时在线。</h1><p>SVG 视口负责交互，C++ 负责 100k 实体变换；LOD 和 dirty-region 让旧设备也能保持响应。</p></div><div className="rx-cad-actions"><button onClick={() => void runNative()} disabled={['running', 'cancel-pending'].includes(taskStateRef.current)}>{['running', 'cancel-pending'].includes(taskStateRef.current) ? `变换中 ${task.progress ?? 0}%` : '运行 100k native scene'}</button>{['running', 'cancel-pending'].includes(taskStateRef.current) && <button onClick={() => void cancelNative()} disabled={taskStateRef.current === 'cancel-pending'}>取消 native 场景</button>}</div></header><section className="rx-cad-layout"><div className="rx-cad-viewport"><div className="rx-cad-toolbar"><button onClick={() => setZoom((value) => Math.min(2.4, value + .15))}>＋</button><button onClick={() => setZoom((value) => Math.max(.55, value - .15))}>－</button><button onClick={() => { setZoom(1); setOffset({ x: 0, y: 0 }); }}>RESET</button><span>{Math.round(zoom * 100)}% · 拖拽平移</span></div><svg viewBox="0 0 1050 700" role="img" aria-label="CAD scene viewport" onWheel={(event) => { event.preventDefault(); setZoom((value) => Math.max(.55, Math.min(2.4, value + (event.deltaY < 0 ? .08 : -.08)))); }} onMouseDown={beginPan} onMouseMove={movePan} onMouseUp={endPan} onMouseLeave={endPan}><defs><pattern id="cad-grid" width="40" height="40" patternUnits="userSpaceOnUse"><path d="M40 0H0V40" fill="none" stroke="#29445e" strokeWidth="1" /></pattern></defs><rect width="1050" height="700" fill="#10283e" /><rect width="1050" height="700" fill="url(#cad-grid)" opacity=".65" /><g transform={`translate(${offset.x} ${offset.y}) scale(${zoom})`}>{cadEntities.slice(0, visible).map((entity) => <g key={entity.id} transform={`translate(${entity.x} ${entity.y})`} onClick={() => setSelected(entity.id)} className={selected === entity.id ? 'selected' : ''}>{entity.kind === 0 ? <rect x={-entity.size} y={-entity.size} width={entity.size * 2} height={entity.size * 2} rx="3" /> : entity.kind === 1 ? <circle r={entity.size} /> : <path d={`M${-entity.size} ${entity.size}L0 ${-entity.size}L${entity.size} ${entity.size}Z`} />}<text x={entity.size + 5} y="4">{entity.id}</text></g>)}</g><path d="M40 650H1010" stroke="#79e1c6" strokeWidth="2" opacity=".55" /><text x="48" y="678" fill="#9cc7d1" fontSize="13">VIEWPORT / DIRTY CLIP / {lod}</text></svg></div><aside className="rx-cad-panel"><small>SCENE INSPECTOR</small><h2>Native geometry pipeline</h2><dl><div><dt>entities</dt><dd>100,000 native</dd></div><div><dt>visible preview</dt><dd>{visible} / {cadEntities.length}</dd></div><div><dt>selection</dt><dd>entity-{selected}</dd></div><div><dt>LOD</dt><dd>{lod}</dd></div><div><dt>renderer</dt><dd>{renderer?.rendererBackend ?? 'cpu-safe pending'}</dd></div></dl><div className="rx-cad-status"><i />{task.message || 'viewport ready · select an entity'}</div><code>{task.metrics || 'spatial index · transform worker · bounded bridge'}</code></aside></section></main>;
}

const compatRows = [
  ['Optional chaining / async-await', 'transpiled', 'Vite legacy + Babel/core-js'],
  ['Promise / Map / Set / Proxy', 'native', 'Miniblink runtime probe'],
  ['URL / Blob / FormData', 'polyfilled', 'local feature-gated shim'],
  ['CSS variables / Flexbox / SVG', 'native', 'Chrome49 tested subset'],
  ['ResizeObserver / structuredClone', 'polyfilled', 'bounded local fallback'],
  ['WebAssembly / Worker / WebGL', 'feature-gated', 'native adapter or explicit fallback'],
  ['BigInt / ES Modules / WebGPU', 'unsupported', 'adjust requirement or upgrade runtime'],
];

function WebCompat() {
  const { data } = useRendererStats();
  return <main className="rx-page rx-compat subpixel-antialiased"><header className="rx-compat-head"><div><small>WEB COMPATIBILITY / OFFLINE RUNTIME</small><h1>现代 TypeScript，落到 Chrome49 可执行产物。</h1><p>开发者可以使用 React、TypeScript 和现代语法；构建系统负责转译，运行时能力明确分级。</p></div><div className="rx-compat-score"><strong>104/104</strong><span>runtime checks passed</span></div></header><section className="rx-compat-layout"><div className="rx-compat-card"><div className="rx-compat-card-head"><b>Capability matrix</b><span>file:// · no CDN · no dev server</span></div>{compatRows.map(([name, status, detail]) => <div className="rx-compat-row" key={name}><span>{name}</span><b className={`status-${status}`}>{status}</b><small>{detail}</small></div>)}</div><aside className="rx-compat-side"><small>FIXED STACK</small><h2>React runtime</h2><p>React 18.3 · Vite 5.4 · Tailwind 3.4 · PrimeReact 10.9 · Zustand · TanStack Query</p><dl><div><dt>target</dt><dd>Chrome 49 legacy</dd></div><div><dt>renderer</dt><dd>{data?.rendererBackend ?? 'software-safe'}</dd></div><div><dt>arch</dt><dd>{data?.architecture ?? 'x86/x64'}</dd></div><div><dt>assets</dt><dd>396 offline files</dd></div></dl><span className="rx-compat-note">无法安全转译的能力不会被伪装成支持；通过 native adapter 或产品降级处理。</span></aside></section></main>;
}

const clearNotice = () => useAppStore.getState().setNotice('');

function App() {
  const { page, setPage, notice, setNotice } = useAppStore();
  const { data: renderer } = useRendererStats();
  useEffect(() => {
    if (import.meta.env.MODE === 'performance') {
      (window as Window & { __souiReactPage?: string }).__souiReactPage = page;
    }
  }, [page]);
  useEffect(() => {
    if (!nativeClient) return;
    const labels: Record<Page, string> = {
      home: '首页', converter: '视频转换', video4k: '4K 播放器', components: '组件展厅',
      motion: '动效体验', data: '万条数据', cad: 'CAD 工作台', compat: '运行环境', fileinsight: '文件洞察',
    };
    // Keep the Windows caption owned by the native host while navigation stays
    // in React. This exercises the standard window contract without exposing
    // HWNDs or allowing page code to mutate the title directly.
    void nativeClient.call('desktop.window.setTitle', { title: `迅捷视频转换器 · ${labels[page]}` }).catch(() => undefined);
  }, [page]);
  const componentsFallback = <main className="rx-page rx-components"><div className="rx-component-head"><div><small>COMPONENT GALLERY</small><h1>组件工作台加载中…</h1></div></div></main>;
  return <><ProductTitlebar page={page} navigate={setPage} action={(method) => { void windowAction(method); }} notify={setNotice} />{page === 'home' ? <ProductHome navigate={setPage} notify={setNotice} openMedia={useAppStore.getState().openMedia} /> : page === 'converter' ? <Converter /> : page === 'video4k' ? <VideoStudio /> : page === 'motion' ? <MotionLab /> : page === 'data' ? <DataInteractionLab task={<PublicSdkTask interaction />} /> : page === 'cad' ? <CadStudio /> : page === 'compat' ? <WebCompat /> : page === 'fileinsight' ? <FileInsight client={nativeClient} onBack={() => setPage('home')} /> : <Suspense fallback={componentsFallback}><LazyComponentsPage renderer={renderer} client={nativeClient} onNotice={setNotice} /></Suspense>}<ProductFeedback notice={notice} clear={clearNotice} /></>;
}

export function mountReactSurface(client?: SouiNativeClientImpl | null): void {
  if (reactMounted) return;
  reactMounted = true;
  nativeClient = client ?? (() => { try { return createReactNativeClient(); } catch { return null; } })();
  const reactRoot = document.getElementById('react-app') ?? document.getElementById('app');
  if (!reactRoot) throw new Error('React root element is missing.');
  // Let React own its container. Replacing innerHTML during a dev hot update
  // detaches React's existing nodes and makes its next commit throw removeChild.
  try {
    // React 18 still exposes the synchronous renderer.  Miniblink's Chrome49
    // event loop does not provide the scheduler primitives required by
    // createRoot's concurrent commit, which otherwise leaves the root empty
    // even though the module and bridge probe have completed.
    render(<ReactSurfaceErrorBoundary><QueryClientProvider client={queryClient}><App /></QueryClientProvider></ReactSurfaceErrorBoundary>, reactRoot);
    // Miniblink keeps the renderer process alive across wkeReload(). Give the
    // native host a synchronous teardown hook so React effects and QueryClient
    // refetch timers are released before the old document is replaced.
    const reloadHost = window as Window & { __souiBeforeReload?: () => void };
    reloadHost.__souiBeforeReload = () => {
      try { queryClient.clear(); } catch { /* cleanup must not block reload */ }
      try { unmountComponentAtNode(reactRoot); } catch { /* stale roots are already detached */ }
      reloadHost.__souiBeforeReload = undefined;
    };
    void reportReactRuntime(nativeClient).then(() => reportReactPerformance(nativeClient));
  } catch (error) {
    reactRoot.textContent = `React 启动失败：${error instanceof Error ? error.message : String(error)}`;
  }
}

async function reportReactRuntime(client: SouiNativeClientImpl | null): Promise<void> {
  if (!client) return;
  try {
    const probeButton = document.createElement('button');
    let domEvent = false;
    probeButton.addEventListener('click', () => { domEvent = true; });
    probeButton.click();
    let errorHandling = false;
    try { JSON.parse('{invalid'); } catch { errorHandling = true; }
    await client.call('bridge.probe.report', {
      script: true,
      json: JSON.parse('{"ok":true}').ok === true,
      event: typeof document.addEventListener === 'function',
      errorHandling,
      localResource: location.protocol === 'file:',
      proxy: typeof Proxy === 'function',
      promise: typeof Promise === 'function',
      symbol: typeof Symbol === 'function',
      reflect: typeof Reflect === 'object',
      vueMount: ((document.getElementById('react-app') ?? document.getElementById('app'))?.childElementCount ?? 0) > 0,
      reactivity: true,
      domEvent,
      bridge: typeof (window as Window & { __souiNativePostMessage?: unknown }).__souiNativePostMessage === 'function',
    });
    document.documentElement.setAttribute('data-soui-react-ready', 'true');
  } catch (error) {
    (window as Window & { __souiReactBootstrapError?: string }).__souiReactBootstrapError = error instanceof Error ? error.message : String(error);
  }
}
// Performance builds need the same bounded, host-readable completion marker as
// the Vue compatibility entry.  Keep this path diagnostic-only so a normal
// offline product launch pays no extra bridge or timing cost.
async function reportReactPerformance(client: SouiNativeClientImpl | null): Promise<void> {
  if (import.meta.env.MODE !== 'performance' || !client) return;
  const timings: number[] = [];
  const pingTimings: number[] = [];
  const destination = window as Window & { __souiPerformanceResult?: unknown };
  try {
    for (let sample = 0; sample < 100; sample++) {
      const started = performance.now();
      await client.call('bridge.probe.report', {
        script: true,
        json: true,
        event: typeof document.addEventListener === 'function',
        errorHandling: true,
        localResource: location.protocol === 'file:',
        proxy: typeof Proxy === 'function',
        promise: typeof Promise === 'function',
        symbol: typeof Symbol === 'function',
        reflect: typeof Reflect === 'object',
        vueMount: ((document.getElementById('react-app') ?? document.getElementById('app'))?.childElementCount ?? 0) > 0,
        reactivity: true,
        domEvent: true,
        bridge: typeof (window as Window & { __souiNativePostMessage?: unknown }).__souiNativePostMessage === 'function',
      });
      timings.push(performance.now() - started);
    }
    if (import.meta.env.VITE_PING_CHECK === 'true') {
      for (let sample = 0; sample < 100; sample++) {
        const started = performance.now();
        await client.call('bridge.benchmark.ping', {});
        pingTimings.push(performance.now() - started);
      }
    }

    // The React endurance path is opt-in and diagnostic-only. It runs the
    // visible FileInsight page through the standard SDK, keeps only bounded
    // cycle summaries, and deliberately crosses the fixed native 30 minute
    // permission TTL. On expiry it invokes the same explicit recovery path as
    // the toolbar button, then resumes on a freshly bootstrapped session.
    const performanceHost = window as Window & {
      __souiBenchmarkRoot?: string;
      __souiReactScanDriver?: (token: string) => Promise<{
        files: number; totalBytes: number; pages: number; progressEvents: number; scanReadyMs: number; totalMs: number; boundaryScroll: 'passed';
      }>;
      __souiReactRecoverSessionForBenchmark?: () => void;
      __souiReactSessionId?: string;
    };
    const enduranceSeconds = Number(import.meta.env.VITE_BENCHMARK_SECONDS ?? '0');
    if (import.meta.env.VITE_REACT_ENDURANCE === 'true' && enduranceSeconds > 0) {
      const recoveryPrefix = 'soui-react-expiry:';
      let previous: { oldSession?: string; elapsedMs?: number } | null = null;
      if (window.name.indexOf(recoveryPrefix) === 0) {
        try { previous = JSON.parse(window.name.slice(recoveryPrefix.length)) as { oldSession?: string; elapsedMs?: number }; }
        catch { throw new Error('React expiry recovery state is invalid.'); }
      }
      const initialElapsedMs = Math.max(0, Number(previous?.elapsedMs ?? 0));
      const enduranceStarted = performance.now();
      const cycles: Array<{ files: number; boundaryScroll: string; totalMs: number; pages: number; progressEvents: number }> = [];
      const retainCycle = (value: Awaited<ReturnType<NonNullable<typeof performanceHost.__souiReactScanDriver>>>) => {
        const writer = (window as Window & { __souiBenchmarkWriteCycle?: (json: string) => boolean }).__souiBenchmarkWriteCycle;
        if (!writer || !writer(JSON.stringify(value))) throw new Error('React endurance cycle could not be persisted.');
        cycles.push({ files: value.files, boundaryScroll: value.boundaryScroll, totalMs: value.totalMs, pages: value.pages, progressEvents: value.progressEvents });
      };
      const driverDeadline = performance.now() + 5000;
      while (!performanceHost.__souiReactScanDriver && performance.now() < driverDeadline) {
        await new Promise<void>((resolve) => window.setTimeout(resolve, 25));
      }
      const driver = performanceHost.__souiReactScanDriver;
      const rootToken = performanceHost.__souiBenchmarkRoot;
      if (!driver || !rootToken) {
        const page = (window as Window & { __souiReactPage?: string }).__souiReactPage ?? '';
        throw new Error(`React endurance driver or benchmark root is unavailable (driver=${Boolean(driver)}, root=${Boolean(rootToken)}, page=${page || 'unknown'}, mode=${import.meta.env.MODE}).`);
      }
      while (initialElapsedMs + performance.now() - enduranceStarted < enduranceSeconds * 1000) {
        try {
          retainCycle(await driver(rootToken));
        } catch (error) {
          if (isSessionExpired(error) && !previous) {
            const elapsedMs = initialElapsedMs + performance.now() - enduranceStarted;
            window.name = recoveryPrefix + JSON.stringify({ oldSession: performanceHost.__souiReactSessionId ?? '', elapsedMs });
            const recover = performanceHost.__souiReactRecoverSessionForBenchmark;
            if (!recover) throw new Error('React expiry recovery action is not available.');
            recover();
            return;
          }
          throw error;
        }
        await new Promise<void>((resolve) => window.setTimeout(resolve, 1000));
      }
      const currentSession = performanceHost.__souiReactSessionId ?? '';
      if (previous) {
        if (!previous.oldSession || !currentSession || previous.oldSession === currentSession) {
          throw new Error('React expiry recovery did not create a fresh native session.');
        }
        window.name = '';
      }
      destination.__souiPerformanceResult = {
        status: 'measured',
        method: 'React FileInsight standard SDK endurance',
        scope: 'Real C++ scan, bounded pagination, progress subscription, fixed 30 minute TTL refusal and explicit reload recovery; no clock or TTL override',
        timings,
        pingTimings,
        cycles,
        elapsedMs: initialElapsedMs + performance.now() - enduranceStarted,
        sessionExpiry: previous ? { observed: true, oldSession: previous.oldSession, currentSession, recovered: true, newTask: cycles.length > 0 } : { observed: false },
      };
      return;
    }
    destination.__souiPerformanceResult = {
      status: 'measured',
      method: 'bridge.probe.report',
      scope: 'React SDK roundtrip and local DOM probe preparation; not empty-call latency',
      timings,
      pingTimings,
      cycles: [],
      elapsedMs: timings.reduce((sum, value) => sum + value, 0),
    };
  } catch (error) {
    destination.__souiPerformanceResult = {
      status: 'failed',
      error: String(error),
      timings,
      pingTimings,
      cycles: [],
    };
  }
}

// React's Vite entry is also the offline Miniblink entry.  Keep the mount
// self-contained so a normal VS/F5 launch exercises React rather than leaving
// an exported function unused.  The short retry covers hosts that install the
// native bridge immediately after the document callback.
if (typeof document !== 'undefined') {
  let attempts = 0;
  const start = () => {
    const nativePostMessage = (window as Window & { __souiNativePostMessage?: unknown }).__souiNativePostMessage;
    if (typeof nativePostMessage !== 'function' && attempts++ < 40) {
      window.setTimeout(start, 25);
      return;
    }
    mountReactSurface();
  };
  if (document.readyState === 'loading') document.addEventListener('DOMContentLoaded', start, { once: true });
  else start();
}
