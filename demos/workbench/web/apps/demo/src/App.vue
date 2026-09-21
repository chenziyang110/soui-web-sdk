<script setup lang="ts">
import { computed, ref, watch, nextTick, onBeforeUnmount, onMounted, defineAsyncComponent } from 'vue';
import InteractionStudio from './InteractionStudio.vue';
import ProductShowcase from './ProductShowcase.vue';
import VideoConverterShowcase from './VideoConverterShowcase.vue';
import ReferenceHomeShowcase from './ReferenceHomeShowcase.vue';
import FrameMeter from './FrameMeter.vue';
const ComponentGallery = defineAsyncComponent(() => import('./ComponentGallery.vue'));
import { KitButton } from './kit-ui';
import './studio.scss';
import { SouiBridgeError, type SouiNativeClientImpl } from '../../../packages/sdk/soui_native_client';

type DemoStatus = 'ready' | 'pending' | 'running' | 'cancel-pending' | 'partial' | 'empty' | 'error' | 'cancelled' | 'timeout' | 'stale';
type Entry = { path: string; directory: boolean; size: number };
type TaskEventPayload = { state?: string; entries?: Entry[]; progress?: number; indeterminate?: boolean; skipped?: number; errors?: number; error?: string; message?: string };
type PageResult = { entries?: unknown[]; cursor?: string; hasMore?: boolean; message?: string; skipped?: number; errors?: number; issues?: string[] };

const props = defineProps<{ nativeClient?: SouiNativeClientImpl | null }>();
// Keep both views mounted: destroying the active Element Plus control during
// its click reproduced a blank surface in the bundled kernel. Also retain settings.
const requestedPage = (window as Window & { __souiInitialPage?: string }).__souiInitialPage;
const validInitialPages = new Set(['home', 'converter', 'showcase', 'studio', 'gallery', 'scan']);
const activePage = ref(validInitialPages.has(requestedPage || '') ? requestedPage! : (import.meta.env.MODE === 'performance' ? 'scan' : 'home'));
const pageOffsets = new Map<string, number>();
let navigationRevision = 0;
watch(activePage, async (page, previous) => {
  const revision = ++navigationRevision;
  pageOffsets.set(previous, window.pageYOffset);
  await nextTick();
  if (!disposed && revision === navigationRevision) window.scrollTo(0, pageOffsets.get(page) ?? 0);
}, { flush: 'pre' });
const galleryOpened = ref(activePage.value === 'gallery');
const converterProfile = ref<{ format: string; codec: string } | undefined>();
const showFrameMeter = import.meta.env.MODE !== 'performance';
const experimentalComponents = import.meta.env.MODE === 'gallery-experiment';
const unmountStudioExperiment = experimentalComponents && import.meta.env.VITE_UNMOUNT_STUDIO === 'true';
const hostArchitecture = ref('unknown');
const rendererBackend = ref('software-skia-gdi');
let rendererRefreshTimer: number | undefined;
async function refreshRendererState() {
  if (!props.nativeClient || disposed) return;
  const result = await props.nativeClient.call('desktop.rendering.getStats', {}).catch(() => undefined);
  if (!result || disposed) return;
  hostArchitecture.value = result.architecture;
  const sharedBackend = (window as Window & { __souiRendererBackend?: string }).__souiRendererBackend;
  rendererBackend.value = sharedBackend || result.rendererBackend || result.gpuStatus || 'software-skia-gdi';
}
onMounted(() => {
  void refreshRendererState();
  rendererRefreshTimer = window.setInterval(() => { void refreshRendererState(); }, 1000);
});
function openGallery() { galleryOpened.value = true; activePage.value = 'gallery'; }
function openConverter(profile?: { format: string; codec: string }) {
  converterProfile.value = profile;
  activePage.value = 'converter';
}
const directoryInput = ref<HTMLInputElement | null>(null);
const detailsToggle = ref<HTMLButtonElement | null>(null);
const directory = ref('');
const filter = ref('');
const composing = ref(false);
const detailsOpen = ref(window.innerWidth > 1100);
const status = ref<DemoStatus>('ready');
const taskId = ref('');
const rootToken = ref('');
const permission = ref('filesystem.read');
const lastError = ref('');
const lastMethod = ref('');
const entries = ref<Entry[]>([
  { path: '示例数据/README.md', directory: false, size: 2048 },
  { path: '示例数据/src', directory: true, size: 0 },
]);
const progress = ref<number | null>(null);
const skippedCount = ref(0);
const errorCount = ref(0);
const issueSummaries = ref<string[]>([]);
const previewData = ref(true);
const nextCursor = ref('');
const hasMore = ref(false);
const pageLoading = ref(false), recovering = ref(false);
let taskRevision = 0, queryRevision = 0, pageRevision = 0;
const resultViewport = ref<HTMLDivElement | null>(null);
const resultScrollTop = ref(0);
const rowHeight = 40;
const virtualized = computed(() => entries.value.length > 200 &&
  !(import.meta.env.MODE === 'performance' && import.meta.env.VITE_DISABLE_VIRTUAL === 'true'));
const firstVisible = computed(() => virtualized.value
  ? Math.min(Math.max(0, entries.value.length - 30), Math.max(0, Math.floor(resultScrollTop.value / rowHeight) - 6)) : 0);
const visibleEntries = computed(() => virtualized.value ? entries.value.slice(firstVisible.value, firstVisible.value + 30) : entries.value);
const topSpacer = computed(() => firstVisible.value * rowHeight);
const bottomSpacer = computed(() => virtualized.value ? Math.max(0, entries.value.length - firstVisible.value - visibleEntries.value.length) * rowHeight : 0);
function onResultScroll() { resultScrollTop.value = resultViewport.value?.scrollTop ?? 0; }
let removeSubscription: (() => void) | undefined;
let disposed = false;
onBeforeUnmount(() => {
  disposed = true;
  if (rendererRefreshTimer !== undefined) window.clearInterval(rendererRefreshTimer);
  rendererRefreshTimer = undefined;
  removeSubscription?.();
  removeSubscription = undefined;
  if (import.meta.env.MODE === 'performance') {
    delete (window as Window & { __souiScanDriver?: unknown }).__souiScanDriver;
  }
});

const statusLabel = computed(() => ({
  ready: '就绪', pending: '请求中', running: '扫描中', 'cancel-pending': '取消中', partial: '部分完成', empty: '空结果', error: '错误', cancelled: '已取消', timeout: '超时观察', stale: '旧会话已失效',
}[status.value]));
const statusHint = computed(() => ({
  ready: '可以选择目录并开始只读扫描。', pending: '正在等待原生端接受任务。', running: '任务由原生端执行，可切换页面；真正重载会结束当前会话。', 'cancel-pending': '已请求取消，等待原生端确认。', partial: `部分条目被跳过（${skippedCount.value}），详情中保留原因。`, empty: '原生端返回了空结果。', error: lastError.value || '原生端返回了错误。', cancelled: '取消请求已被原生端确认。', timeout: '仍在等待原生端最终事实。', stale: '旧会话回调已拒绝，正在恢复任务事实。',
}[status.value]));

function mapNativeState(value: string | undefined): DemoStatus | undefined {
  if (!value) return undefined;
  if (value === 'cancelRequested' || value === 'cancel-pending') return 'cancel-pending';
  if (value === 'timeoutObserved' || value === 'timeout') return 'timeout';
  if (value === 'accepted') return 'pending';
  if (value === 'succeeded' || value === 'success') return entries.value.length === 0 ? 'empty' : 'ready';
  if (value === 'cancelled') return 'cancelled';
  if (value === 'partial') return 'partial';
  if (value === 'failed' || value === 'error') return 'error';
  if (value === 'running') return 'running';
  return undefined;
}

function applyTaskPayload(payload: TaskEventPayload | undefined) {
  if (Array.isArray(payload?.entries)) entries.value = payload.entries;
  const nextState = mapNativeState(payload?.state);
  if (nextState) status.value = nextState;
  if (payload?.indeterminate === true) progress.value = null;
  else if (typeof payload?.progress === 'number') progress.value = Math.max(0, Math.min(100, payload.progress));
  if (typeof payload?.skipped === 'number') skippedCount.value = payload.skipped;
  if (typeof payload?.errors === 'number') errorCount.value = payload.errors;
  if (payload?.error) lastError.value = payload.error;
  if (status.value === 'ready' && entries.value.length === 0) status.value = 'empty';
}

async function refreshTask(id: string) {
  if (!props.nativeClient || disposed) return;
  const owner = taskRevision, query = ++queryRevision;
  const result = await props.nativeClient.query(id).catch(error => {
    if (disposed || owner !== taskRevision || id !== taskId.value || query !== queryRevision) return undefined;
    throw error;
  });
  if (!result || disposed || owner !== taskRevision || id !== taskId.value || query !== queryRevision) return;
  pageRevision++; pageLoading.value = false;
  const page = result as typeof result & PageResult;
  applyTaskPayload({
    state: result.state,
    progress: result.progress,
    entries: Array.isArray(page.entries) ? page.entries as Entry[] : undefined,
    message: page.message,
    skipped: page.skipped,
    errors: page.errors,
  });
  issueSummaries.value = Array.isArray(page.issues) ? page.issues.slice(0, 20) : [];
  nextCursor.value = page.cursor ?? '';
  hasMore.value = page.hasMore === true && ['succeeded', 'partial'].includes(result.state);
}

function subscribeTask(id: string) {
  if (disposed) return;
  const owner = taskRevision;
  removeSubscription?.();
  removeSubscription = props.nativeClient?.subscribe(id, 'task.progress', (event) => {
    if (disposed || owner !== taskRevision || id !== taskId.value) return;
    const payload = event.payload as TaskEventPayload;
    applyTaskPayload(payload);
    if (['succeeded', 'partial', 'cancelled', 'failed'].includes(payload.state ?? '')) {
      void refreshTask(id).catch((error) => { if (!disposed && owner === taskRevision && id === taskId.value) setRequestError(error, '读取扫描结果失败。'); });
    }
  });
}

function setError(message: string) {
  lastError.value = message;
  status.value = 'error';
}

function setRequestError(error: unknown, fallback: string) {
  lastError.value = error instanceof SouiBridgeError && error.code === 'E_SESSION_EXPIRED'
    ? '会话授权已到期，请在“交互体验 → 展开详情”中重新连接，再选择目录。旧任务不会自动恢复。'
    : error instanceof Error ? error.message : fallback;
  status.value = error instanceof SouiBridgeError && error.code === 'E_TIMEOUT' ? 'timeout' : 'error';
}

async function selectDirectory() {
  lastMethod.value = 'desktop.dialog.pickDirectory';
  if (!props.nativeClient) {
    setError('示例模式：未连接真实 SOUI/Miniblink 主机。');
    return;
  }
  try {
    const result = await props.nativeClient.call('desktop.dialog.pickDirectory', {
      title: '选择扫描目录',
      userGesture: true,
    });
    if (result.cancelled) {
      status.value = 'ready';
      lastError.value = '用户取消了目录选择。';
      rootToken.value = '';
      return;
    }
    rootToken.value = result.rootToken;
    directory.value = '已由原生端授权的目录';
    status.value = 'ready';
    lastError.value = '';
  } catch (error) {
    setRequestError(error, '目录选择失败。');
  } finally {
    setTimeout(() => directoryInput.value?.focus(), 0);
  }
}

async function startScan() {
  if (disposed || recovering.value || ['pending', 'running', 'cancel-pending'].includes(status.value)) return;
  lastMethod.value = 'demo.scan.start';
  if (!props.nativeClient || !rootToken.value) {
    setError(props.nativeClient ? '请先选择目录。' : '示例模式：未连接真实 SOUI/Miniblink 主机。');
    return;
  }
  const owner = ++taskRevision;
  queryRevision++; pageRevision++; pageLoading.value = false;
  removeSubscription?.(); removeSubscription = undefined;
  taskId.value = '';
  try {
    status.value = 'pending';
    entries.value = [];
    resultScrollTop.value = 0;
    if (resultViewport.value) resultViewport.value.scrollTop = 0;
    skippedCount.value = 0;
    errorCount.value = 0;
    issueSummaries.value = [];
    progress.value = null;
    previewData.value = false;
    nextCursor.value = '';
    hasMore.value = false;
    lastError.value = '';
    const result = await props.nativeClient.call('demo.scan.start', { rootToken: rootToken.value, filter: filter.value, operationId: `demo-scan-${Date.now()}` });
    if (disposed || owner !== taskRevision) return;
    taskId.value = result.taskId;
    status.value = result.accepted ? 'running' : 'error';
    if (!result.accepted) lastError.value = '原生端未接受扫描任务。';
    subscribeTask(taskId.value);
    await refreshTask(taskId.value);
  } catch (error) {
    if (!disposed && owner === taskRevision) setRequestError(error, '启动扫描失败。');
  }
}

async function cancelScan() {
  if (disposed || status.value === 'cancel-pending') return;
  lastMethod.value = 'task.cancel';
  if (!props.nativeClient || !taskId.value) {
    setError('没有可取消的真实任务。');
    return;
  }
  const id = taskId.value, owner = taskRevision;
  try {
    status.value = 'cancel-pending';
    const result = await props.nativeClient.cancel(id);
    if (disposed || owner !== taskRevision || id !== taskId.value || status.value !== 'cancel-pending') return;
    status.value = result.cancelRequested ? 'cancel-pending' : mapNativeState(result.state) ?? 'cancel-pending';
  } catch (error) {
    if (disposed || owner !== taskRevision || id !== taskId.value) return;
    if (error instanceof SouiBridgeError && error.code === 'E_TASK_TERMINAL') {
      await refreshTask(taskId.value);
      return;
    }
    setRequestError(error, '取消扫描失败。');
  }
}

async function reconnectTask() {
  if (disposed || recovering.value) return;
  lastMethod.value = 'task.query+resubscribe';
  if (!props.nativeClient || !taskId.value) {
    setError('示例模式：没有可恢复的真实任务。');
    return;
  }
  const id = taskId.value, owner = taskRevision;
  recovering.value = true;
  try {
    status.value = 'stale';
    removeSubscription?.();
    await refreshTask(id);
    if (disposed || owner !== taskRevision || id !== taskId.value) return;
    subscribeTask(id);
    directoryInput.value?.focus();
  } catch (error) {
    if (!disposed && owner === taskRevision) setRequestError(error, '任务恢复失败。');
  } finally { if (!disposed && owner === taskRevision) recovering.value = false; }
}

async function loadMore() {
  if (!props.nativeClient || disposed || !taskId.value || !hasMore.value || pageLoading.value || recovering.value) return;
  const id = taskId.value, owner = taskRevision, pageRequest = ++pageRevision;
  pageLoading.value = true;
  lastMethod.value = 'demo.scan.page';
  try {
    const page = await props.nativeClient.call('demo.scan.page', {
      taskId: id,
      cursor: nextCursor.value,
      pageSize: 100,
    }) as PageResult;
    if (disposed || owner !== taskRevision || id !== taskId.value || pageRequest !== pageRevision) return;
    if (Array.isArray(page.entries)) entries.value.push(...page.entries as Entry[]);
    nextCursor.value = page.cursor ?? '';
    hasMore.value = page.hasMore === true;
  } catch (error) {
    if (!disposed && owner === taskRevision && pageRequest === pageRevision) setRequestError(error, '读取下一页失败。');
  } finally { if (!disposed && owner === taskRevision && pageRequest === pageRevision) pageLoading.value = false; }
}

function onFilterEnter(event: KeyboardEvent) {
  if (composing.value || event.isComposing) return;
  void startScan();
}

function onEscape() {
  if (detailsOpen.value) {
    detailsOpen.value = false;
    detailsToggle.value?.focus();
  }
}

if (import.meta.env.MODE === 'performance') {
  (window as Window & { __souiScanDriver?: (token: string) => Promise<unknown> }).__souiScanDriver = async (token) => {
    rootToken.value = token;
    directory.value = '已授权的 10,000 文件测试目录';
    filter.value = '*.txt';
    const frames: number[] = [];
    const pageTimes: number[] = [];
    let observing = true;
    let lastFrame = performance.now();
    const frame = (time: number) => { frames.push(time - lastFrame); lastFrame = time; if (observing) requestAnimationFrame(frame); };
    requestAnimationFrame(frame);
    const started = performance.now();
    try {
      await startScan();
      if (!taskId.value) throw new Error(lastError.value || 'No task accepted');
      while (true) {
        const state = await props.nativeClient!.query(taskId.value);
        if (state.state === 'succeeded') break;
        if (performance.now() - started > 15000) throw new Error('Scan deadline');
        await new Promise(resolve => setTimeout(resolve, 50));
      }
      await refreshTask(taskId.value);
      const scanReadyMs = performance.now() - started;
      while (hasMore.value) {
        const pageStarted = performance.now();
        await loadMore();
        await nextTick();
        await new Promise(resolve => requestAnimationFrame(resolve));
        pageTimes.push(performance.now() - pageStarted);
        if (status.value === 'error') throw new Error(lastError.value);
      }
      const files = entries.value.filter(entry => !entry.directory);
      if (files.length !== 10000 || new Set(files.map(entry => entry.path)).size !== 10000) throw new Error('Dataset mismatch');
      const totalMs = performance.now() - started;
      const viewport = resultViewport.value;
      if (!viewport) throw new Error('Result viewport missing');
      viewport.scrollTop = viewport.scrollHeight;
      onResultScroll();
      await nextTick();
      const lastPath = entries.value[entries.value.length - 1].path;
      if (!Array.from(document.querySelectorAll('tr[data-path]')).some(row => row.getAttribute('data-path') === lastPath)) throw new Error('Last row unreachable');
      viewport.scrollTop = 0;
      onResultScroll();
      await nextTick();
      if (document.querySelector('tr[data-path]')?.getAttribute('data-path') !== entries.value[0].path) throw new Error('First row unreachable');
      const scrollSteps: number[] = [];
      const scrollActionTimes: number[] = [];
      if (import.meta.env.VITE_SCROLL_CHECK === 'true') {
        const paintHost = window as Window & { __souiBenchmarkMarkPhase?: (phase: string) => void };
        paintHost.__souiBenchmarkMarkPhase?.('scroll');
        if (import.meta.env.VITE_SCROLL_CLOCK === 'interval') {
          await new Promise<void>((resolve) => {
            let step = 0;
            const timer = setInterval(() => {
              scrollActionTimes.push(performance.now());
              viewport.scrollTop = (viewport.scrollHeight - viewport.clientHeight) * step / 119;
              onResultScroll();
              if (++step === 120) { clearInterval(timer); resolve(); }
            }, 16);
          });
          await nextTick();
          await new Promise(resolve => requestAnimationFrame(resolve));
        } else for (let step = 0; step < 120; step++) {
          const stepStarted = performance.now();
          scrollActionTimes.push(stepStarted);
          viewport.scrollTop = (viewport.scrollHeight - viewport.clientHeight) * step / 119;
          onResultScroll();
          await nextTick();
          await new Promise(resolve => requestAnimationFrame(resolve));
          scrollSteps.push(performance.now() - stepStarted);
          await new Promise(resolve => setTimeout(resolve, 16));
        }
        paintHost.__souiBenchmarkMarkPhase?.('idle');
      }
      return { files: files.length, domRows: document.querySelectorAll('tbody tr').length, boundaryScroll: 'passed',
        scanReadyMs, totalMs, pageTimes, frames, scrollSteps, scrollActionTimes,
        scope: 'real App startScan/loadMore, SDK, native scanner and Vue DOM updates; rAF intervals are not presented-frame timings' };
    } finally { observing = false; }
  };
}
</script>

<template>
  <main class="workbench" :class="{ 'workbench-converter': activePage === 'converter', 'workbench-home': activePage === 'home' }" :aria-labelledby="activePage === 'home' ? 'reference-home-title' : 'page-title'" @keydown.esc="onEscape">
    <nav class="sidebar" aria-label="模块导航">
      <div class="brand"><span class="brand-mark">S</span><span>SOUI Web Kit</span></div>
      <p class="eyebrow">DEVELOPER WORKBENCH</p>
      <button class="nav-item" :class="{ selected: activePage === 'home' }" type="button" :aria-current="activePage === 'home' ? 'page' : undefined" @click="activePage = 'home'">产品首页</button>
      <button class="nav-item" :class="{ selected: activePage === 'converter' }" type="button" :aria-current="activePage === 'converter' ? 'page' : undefined" @click="activePage = 'converter'">视频转换器</button>
      <button class="nav-item" :class="{ selected: activePage === 'showcase' }" type="button" :aria-current="activePage === 'showcase' ? 'page' : undefined" @click="activePage = 'showcase'">产品工作台</button>
      <button class="nav-item" :class="{ selected: activePage === 'studio' }" type="button" :aria-current="activePage === 'studio' ? 'page' : undefined" @click="activePage = 'studio'">交互体验</button>
      <button class="nav-item" :class="{ selected: activePage === 'gallery' }" type="button" :aria-current="activePage === 'gallery' ? 'page' : undefined" @click="openGallery">全部组件</button>
      <button class="nav-item" :class="{ selected: activePage === 'scan' }" type="button" :aria-current="activePage === 'scan' ? 'page' : undefined" @click="activePage = 'scan'">原生目录扫描</button>
      <div class="sidebar-footer"><span class="dot" aria-hidden="true"></span><span>本地资源模式</span></div>
    </nav>

    <ReferenceHomeShowcase v-show="activePage === 'home'" :native-connected="!!nativeClient" :native-client="nativeClient" :renderer-backend="rendererBackend" :architecture="hostArchitecture" @converter="openConverter" @scan="activePage = 'scan'" @gallery="openGallery" />
    <VideoConverterShowcase v-show="activePage === 'converter'" :active="activePage === 'converter'" :native-connected="!!nativeClient" :native-client="nativeClient" :renderer-backend="rendererBackend" :architecture="hostArchitecture" :initial-profile="converterProfile" @scan="activePage = 'scan'" @gallery="openGallery" />
    <ProductShowcase v-show="activePage === 'showcase'" :active="activePage === 'showcase'" :native-client="nativeClient" :native-connected="!!nativeClient" :renderer-backend="rendererBackend" :architecture="hostArchitecture" @scan="activePage = 'scan'" @gallery="openGallery" />
    <InteractionStudio v-if="!(unmountStudioExperiment && activePage === 'gallery')" v-show="activePage === 'studio'" :native-connected="!!nativeClient" :native-client="nativeClient" :renderer-backend="rendererBackend" @scan="activePage = 'scan'" />
    <ComponentGallery v-if="galleryOpened" v-show="activePage === 'gallery'" :active="activePage === 'gallery'" :architecture="hostArchitecture" :experimental-all="experimentalComponents" />
    <FrameMeter v-if="showFrameMeter && activePage !== 'home'" :native-client="nativeClient" />
    <section v-show="activePage === 'scan'" class="content">
      <header class="page-header">
        <div>
          <p class="eyebrow">SOUI + MINIBLINK / DEMO</p>
          <h1 id="page-title">只读目录扫描</h1>
          <p class="lede">选择一个授权目录，观察原生任务的进度、结果和异常。</p>
        </div>
        <span v-if="previewData" class="preview-badge">示例数据</span>
      </header>

      <section class="action-card" aria-labelledby="action-title">
        <div class="card-heading"><div><span class="step">01</span><h2 id="action-title">选择扫描范围</h2></div><span class="permission-chip">权限 · {{ permission }}</span></div>
        <div class="form-row">
          <label class="field-label" for="directory">目录</label>
          <input id="directory" ref="directoryInput" v-model="directory" type="text" placeholder="请通过原生对话框选择目录" @compositionstart="composing = true" @compositionend="composing = false">
          <KitButton @click="selectDirectory">选择目录</KitButton>
        </div>
        <div class="form-row filter-row">
          <label class="field-label" for="filter">过滤</label>
          <input id="filter" v-model="filter" type="text" placeholder="例如：*.cpp" @compositionstart="composing = true" @compositionend="composing = false" @keydown.enter="onFilterEnter">
          <KitButton type="primary" :loading="['pending', 'running', 'cancel-pending'].includes(status)" :disabled="recovering || ['pending', 'running', 'cancel-pending'].includes(status)" @click="startScan">开始扫描</KitButton>
        </div>
      </section>

      <section class="status-card" :class="`status-${status}`" aria-live="polite">
        <div class="status-icon" aria-hidden="true"><span class="status-glyph"></span></div>
        <div class="status-copy"><div class="status-line"><strong>{{ statusLabel }}</strong><span>{{ statusHint }}</span></div><div class="progress-track"><span :style="{ width: progress === null ? (status === 'running' ? '42%' : status === 'ready' ? '0%' : '100%') : `${progress}%` }"></span></div></div>
        <button v-if="status === 'running' || status === 'timeout'" class="text-button" type="button" @click="cancelScan">取消</button>
        <span v-else-if="status === 'cancel-pending'" class="text-button pending-label">取消中</span>
      </section>

      <section class="results-card" aria-labelledby="results-title">
        <div class="card-heading"><div><span class="step">02</span><h2 id="results-title">扫描结果</h2></div><div class="result-actions"><span class="result-count">{{ entries.length }} 条</span><button v-if="hasMore" class="text-button" type="button" :disabled="pageLoading || recovering" @click="loadMore">{{ pageLoading ? '加载中' : '加载更多' }}</button></div></div>
        <div v-if="status === 'empty'" class="empty-state">原生任务没有返回条目。</div>
        <div v-else ref="resultViewport" class="table-wrap" :class="{ 'virtual-list': entries.length > 200 }" tabindex="0" aria-label="扫描结果，可使用方向键滚动" @scroll="onResultScroll"><table :aria-rowcount="entries.length + 1"><thead><tr><th scope="col">路径</th><th scope="col">类型</th><th scope="col">大小</th></tr></thead><tbody><tr v-if="topSpacer" aria-hidden="true" class="spacer-row"><td colspan="3" :style="{ height: `${topSpacer}px` }"></td></tr><tr v-for="(entry, index) in visibleEntries" :key="entry.path" class="data-row" :data-path="entry.path" :aria-rowindex="firstVisible + index + 2"><td :title="entry.path"><span class="path-icon" :class="entry.directory ? 'path-directory' : 'path-file'" aria-hidden="true"></span>{{ entry.path }}</td><td>{{ entry.directory ? '目录' : '文件' }}</td><td>{{ entry.directory ? '—' : `${entry.size} B` }}</td></tr><tr v-if="bottomSpacer" aria-hidden="true" class="spacer-row"><td colspan="3" :style="{ height: `${bottomSpacer}px` }"></td></tr></tbody></table></div>
      </section>
    </section>

    <aside v-show="activePage === 'scan'" class="details" :class="{ collapsed: !detailsOpen }" aria-label="调用详情">
      <button ref="detailsToggle" class="details-toggle" type="button" :aria-expanded="detailsOpen" @click="detailsOpen = !detailsOpen"><span>调用详情</span><span class="detail-chevron" :class="{ open: detailsOpen }" aria-hidden="true"></span></button>
      <div v-if="detailsOpen" class="details-body"><p class="eyebrow">NATIVE FACTS</p><dl><div><dt>方法</dt><dd>{{ lastMethod || '—' }}</dd></div><div><dt>任务 ID</dt><dd>{{ taskId || '—' }}</dd></div><div><dt>权限</dt><dd>{{ permission }}</dd></div><div><dt>跳过/错误</dt><dd>{{ skippedCount }} / {{ errorCount }}</dd></div><div v-if="issueSummaries.length"><dt>部分原因</dt><dd><ul class="issue-list"><li v-for="issue in issueSummaries" :key="issue">{{ issue }}</li></ul></dd></div><div><dt>结果</dt><dd>{{ statusLabel }}</dd></div></dl><button class="secondary-button full" type="button" :disabled="!nativeClient || !taskId || recovering" @click="reconnectTask">重新查询并订阅</button><p class="details-note">只重新查询当前会话的任务，不重载页面，也不续期授权。关闭窗口或真正重载页面会结束旧会话并清理其扫描任务。</p></div>
    </aside>
  </main>
</template>
