<script setup lang="ts">
import { computed, onBeforeUnmount, onMounted, ref, watch } from 'vue';
import type { SouiNativeClientImpl } from '../../../packages/sdk/soui_native_client';
import { KitButton } from './kit-ui';
import sampleMp4Url from './media/aurora-sample.mp4';
import sample4kMp4Url from './media/aurora-4k-sample.mp4';
import sampleWebmUrl from './media/aurora-sample.webm';
import sampleGifUrl from './media/aurora-sample.gif';
import samplePosterUrl from './media/aurora-poster.png';

const props = defineProps<{
  nativeConnected: boolean;
  nativeClient?: SouiNativeClientImpl | null;
  rendererBackend?: string;
  architecture?: string;
  active?: boolean;
  initialProfile?: { format: string; codec: string };
}>();
const emit = defineEmits<{
  (event: 'scan'): void;
  (event: 'gallery'): void;
}>();

const formatProfiles = [
  { format: 'MP4', codec: 'H.264' },
  { format: 'WebM', codec: 'VP9' },
  { format: 'MOV', codec: 'H.264' },
  { format: 'GIF', codec: 'GIF' },
  { format: 'WebP', codec: 'WebP' },
  { format: 'PNG', codec: 'PNG' },
  { format: 'MP3', codec: 'MP3' },
  { format: 'WAV', codec: 'PCM' },
];
const formats = formatProfiles.map(profile => profile.format);
const selectedFormat = ref('MP4');
const selectedCodec = ref('H.264');
const codecs = computed(() => formatProfiles.filter(profile => profile.format === selectedFormat.value).map(profile => profile.codec));
const quality = ref(82);
const previewPlaying = ref(false);
const converting = ref(false);
const progress = ref(0);
const outputReady = ref(false);
const statusNote = ref('准备就绪 · 可离线处理');
const bridgeProbe = ref('等待原生能力读取');
const videoElement = ref<HTMLVideoElement | null>(null);
const videoCurrentTime = ref(0);
const videoDuration = ref(3);
const videoFallback = ref(false);
const fileInput = ref<HTMLInputElement | null>(null);
const sourceName = ref('Aurora 产品片头.mp4');
const sourceDetails = ref('326 KB · 640×360 · 30 fps');
const sourceObjectUrl = ref('');
const userSource = ref(false);
const nativeFileToken = ref('');
const nativePreviewData = ref('');
const nativePreviewDecoder = ref('');
const nativePreviewTaskId = ref('');
const nativeMediaTaskId = ref('');
const nativeMediaOutput = ref('');
const nativeMediaCapability = ref('');
const nativeSaveTaskId = ref('');
const nativeSaveName = ref('');
const savingOutput = ref(false);
const selectedWorkload = ref('player');
const workloadTaskId = ref('');
const workloadProgress = ref(0);
const workloadState = ref('idle');
const workloadMetrics = ref<Record<string, unknown>>({});
const workloadCapability = ref('');
const queue = ref([
  { name: 'Aurora 产品片头.mp4', detail: '640×360 · 00:03', state: 'ready', color: 'mint' },
  { name: '社媒预告_竖版.mp4', detail: '1080×1920 · 00:32', state: 'queued', color: 'blue' },
]);
let nativeMediaTimer: number | undefined;
let nativeMediaRequesting = false;
let nativePreviewTimer: number | undefined;
let nativePreviewRequesting = false;
let workloadTimer: number | undefined;
let workloadRequesting = false;
let nativeSaveTimer: number | undefined;
let nativeSaveRequesting = false;

const isGpu = computed(() => (props.rendererBackend || '').indexOf('gpu') === 0);
const backendLabel = computed(() => isGpu.value ? 'GPU compositor' : 'CPU-safe raster');
const outputSize = computed(() => {
  const ratio = Math.max(.35, Math.min(1.2, quality.value / 82));
  return `${(0.326 * ratio).toFixed(2)} MB`;
});
const eta = computed(() => converting.value ? `${Math.max(1, Math.ceil((100 - progress.value) / 11))} 秒` : '—');
const workloads = [
  { id: 'player', icon: '◉', title: '4K 视频播放器', eyebrow: 'MEDIA', state: 'UI READY', detail: '合成层、时间轴和 GPU/CPU 降级已具备；硬件解码器由 native adapter 接入。', metric: '4K adapter' },
  { id: 'cad', icon: '⌁', title: 'CAD 编辑器', eyebrow: 'SCENE', state: 'LAYERS READY', detail: '脏区、缩放、平移和输入骨架可复用；几何内核与 DX 资源独立运行。', metric: 'scene graph' },
  { id: 'zip', icon: '▣', title: 'ZIP 解压工具', eyebrow: 'TASKS', state: 'WORKER READY', detail: 'C++ worker、进度合并、取消确认和分页结果可直接复用。', metric: 'native worker' },
];
const activeWorkload = computed(() => workloads.find(item => item.id === selectedWorkload.value) || workloads[0]);

watch(selectedFormat, (format) => {
  const profile = formatProfiles.find(item => item.format === format);
  if (profile) selectedCodec.value = profile.codec;
});
watch(() => props.initialProfile, (profile) => {
  if (!profile || !formatProfiles.some(item => item.format === profile.format && item.codec === profile.codec)) return;
  selectedFormat.value = profile.format;
  selectedCodec.value = profile.codec;
}, { immediate: true });

function formatVideoTime(value: number) {
  const seconds = Math.max(0, Math.floor(Number.isFinite(value) ? value : 0));
  return `00:${String(seconds % 60).padStart(2, '0')}`;
}

function stopNativeMediaTimer() {
  if (nativeMediaTimer !== undefined) window.clearInterval(nativeMediaTimer);
  nativeMediaTimer = undefined;
  nativeMediaRequesting = false;
}

function stopNativeSaveTimer() {
  if (nativeSaveTimer !== undefined) window.clearInterval(nativeSaveTimer);
  nativeSaveTimer = undefined;
  nativeSaveRequesting = false;
}

function stopNativePreviewTimer() {
  if (nativePreviewTimer !== undefined) window.clearInterval(nativePreviewTimer);
  nativePreviewTimer = undefined;
  nativePreviewRequesting = false;
}

function stopWorkloadTimer() {
  if (workloadTimer !== undefined) window.clearInterval(workloadTimer);
  workloadTimer = undefined;
  workloadRequesting = false;
}

function workloadComplexity(kind: string) {
  if (kind === 'player') return 2;
  if (kind === 'cad') return 100000;
  return 32;
}

async function startWorkloadBenchmark() {
  if (!props.nativeClient || workloadState.value === 'running' || workloadState.value === 'cancel-pending') {
    statusNote.value = props.nativeClient ? '原生压力任务正在运行' : '连接 SOUI 宿主后运行原生压力任务';
    return;
  }
  const kind = selectedWorkload.value === 'player' ? 'video4k' : selectedWorkload.value;
  try {
    const started = await props.nativeClient.call('desktop.workload.start', {
      kind, complexity: workloadComplexity(selectedWorkload.value), operationId: `workload-${kind}-${Date.now()}`,
    });
    workloadTaskId.value = started.taskId;
    workloadProgress.value = 0;
    workloadState.value = 'running';
    workloadMetrics.value = {};
    statusNote.value = `${activeWorkload.value.title} 原生任务已启动`;
    workloadTimer = window.setInterval(() => { void pollWorkload(); }, 120);
  } catch (error) {
    workloadState.value = 'failed';
    statusNote.value = `原生压力任务启动失败 · ${error instanceof Error ? error.message : 'Bridge 不可用'}`;
  }
}

async function pollWorkload() {
  if (!props.nativeClient || !workloadTaskId.value || workloadRequesting) return;
  workloadRequesting = true;
  const taskId = workloadTaskId.value;
  try {
    const result = await props.nativeClient.call('desktop.workload.query', { taskId });
    if (taskId !== workloadTaskId.value) return;
    workloadState.value = result.state;
    workloadProgress.value = Number(result.progress || 0);
    try { workloadMetrics.value = result.metrics ? JSON.parse(result.metrics) : {}; } catch { workloadMetrics.value = {}; }
    if (result.state === 'succeeded') {
      stopWorkloadTimer();
      statusNote.value = `${activeWorkload.value.title} 完成 · ${result.message}`;
    } else if (result.state === 'failed' || result.state === 'cancelled') {
      stopWorkloadTimer();
      statusNote.value = `${activeWorkload.value.title} ${result.state === 'cancelled' ? '已取消' : '失败'} · ${result.message}`;
    }
  } catch (error) {
    stopWorkloadTimer();
    workloadState.value = 'failed';
    statusNote.value = `原生压力任务查询失败 · ${error instanceof Error ? error.message : 'Bridge 不可用'}`;
  } finally { workloadRequesting = false; }
}

async function cancelWorkload() {
  if (!props.nativeClient || !workloadTaskId.value) return;
  try {
    await props.nativeClient.call('desktop.workload.cancel', { taskId: workloadTaskId.value });
    workloadState.value = 'cancel-pending';
    statusNote.value = '原生压力任务取消中';
  } catch (error) {
    statusNote.value = `取消失败 · ${error instanceof Error ? error.message : 'Bridge 不可用'}`;
  }
}

function formatBytes(bytes: number) {
  if (bytes < 1024) return `${bytes} B`;
  if (bytes < 1024 * 1024) return `${(bytes / 1024).toFixed(0)} KB`;
  return `${(bytes / (1024 * 1024)).toFixed(1)} MB`;
}

function chooseFile() {
  if (fileInput.value) fileInput.value.click();
}

async function chooseNativeFile() {
  if (!props.nativeClient) {
    statusNote.value = '示例模式：未连接原生文件选择器';
    return;
  }
  try {
    const result = await props.nativeClient.call('desktop.media.pickFile', { userGesture: true });
    if (result.cancelled) {
      statusNote.value = '已取消原生视频选择';
      return;
    }
    nativeFileToken.value = result.fileToken;
    nativePreviewData.value = '';
    nativePreviewDecoder.value = '';
    userSource.value = true;
    sourceName.value = result.name || '原生视频文件';
    sourceDetails.value = `${formatBytes(result.size)} · native file token · MP4`;
    videoFallback.value = true;
    previewPlaying.value = false;
    progress.value = 0;
    outputReady.value = false;
    statusNote.value = '原生视频已授权 · 可执行 MP4/H.264 原样导出';
    queue.value[0].name = sourceName.value;
    queue.value[0].detail = `${formatBytes(result.size)} · native file token`;
    void loadNativePreview(result.fileToken);
  } catch (error) {
    statusNote.value = `原生视频选择失败 · ${error instanceof Error ? error.message : 'Bridge 不可用'}`;
  }
}

function acceptFile(file: File | undefined) {
  if (!file || !/^video\//i.test(file.type)) {
    statusNote.value = '请选择视频文件（MP4 / WebM / MOV）';
    return;
  }
  nativeFileToken.value = '';
  stopNativePreviewTimer();
  nativePreviewTaskId.value = '';
  nativePreviewData.value = '';
  nativePreviewDecoder.value = '';
  if (sourceObjectUrl.value && typeof URL.revokeObjectURL === 'function') URL.revokeObjectURL(sourceObjectUrl.value);
  sourceObjectUrl.value = typeof URL.createObjectURL === 'function' ? URL.createObjectURL(file) : '';
  userSource.value = true;
  sourceName.value = file.name;
  sourceDetails.value = `${formatBytes(file.size)} · 本地文件 · 等待元数据`;
  videoFallback.value = false;
  previewPlaying.value = false;
  progress.value = 0;
  outputReady.value = false;
  if (videoElement.value && sourceObjectUrl.value) {
    videoElement.value.src = sourceObjectUrl.value;
    try { videoElement.value.load(); } catch { /* older Miniblink may not expose load() */ }
  }
  statusNote.value = '本地视频已选择 · 正在读取元数据';
  queue.value[0].name = file.name;
  queue.value[0].detail = `${formatBytes(file.size)} · 本地文件`;
}

function onFilePicked(event: Event) {
  const input = event.target as HTMLInputElement;
  acceptFile(input.files && input.files[0]);
}

function onDrop(event: DragEvent) {
  acceptFile(event.dataTransfer && event.dataTransfer.files && event.dataTransfer.files[0]);
}

function resetSource() {
  if (sourceObjectUrl.value && typeof URL.revokeObjectURL === 'function') URL.revokeObjectURL(sourceObjectUrl.value);
  sourceObjectUrl.value = '';
  sourceName.value = 'Aurora 产品片头.mp4';
  sourceDetails.value = '326 KB · 640×360 · 30 fps';
  userSource.value = false;
  nativeFileToken.value = '';
  stopNativePreviewTimer();
  nativePreviewTaskId.value = '';
  nativePreviewData.value = '';
  nativePreviewDecoder.value = '';
  stopNativeSaveTimer();
  nativeSaveTaskId.value = '';
  nativeSaveName.value = '';
  savingOutput.value = false;
  videoFallback.value = false;
  previewPlaying.value = false;
  if (videoElement.value) {
    videoElement.value.removeAttribute('src');
    try { videoElement.value.load(); } catch { /* older Miniblink may not expose load() */ }
  }
  statusNote.value = '已恢复离线样例视频';
  queue.value[0].name = 'Aurora 产品片头.mp4';
  queue.value[0].detail = '640×360 · 00:03';
}

async function loadNativePreview(source: string) {
  if (!props.nativeClient || !source) return;
  stopNativePreviewTimer();
  nativePreviewTaskId.value = '';
  try {
    const started = await props.nativeClient.call('desktop.media.preview.start', {
      source, operationId: `preview-${source}-${Date.now()}`,
    });
    nativePreviewTaskId.value = started.taskId;
    nativePreviewData.value = '';
    nativePreviewDecoder.value = '';
    nativePreviewTimer = window.setInterval(() => { void pollNativePreview(); }, 120);
    await pollNativePreview();
  } catch {
    nativePreviewData.value = '';
    nativePreviewDecoder.value = '';
  }
}

async function pollNativePreview() {
  if (!props.nativeClient || !nativePreviewTaskId.value || nativePreviewRequesting) return;
  nativePreviewRequesting = true;
  const taskId = nativePreviewTaskId.value;
  try {
    const result = await props.nativeClient.call('desktop.media.preview.query', { taskId });
    if (taskId !== nativePreviewTaskId.value) return;
    if (result.supported && result.frameData) {
      nativePreviewData.value = result.frameData;
      nativePreviewDecoder.value = result.decoder || 'Media Foundation';
      videoFallback.value = true;
      previewPlaying.value = false;
    }
    if (result.state === 'succeeded' || result.state === 'partial' || result.state === 'failed' || result.state === 'cancelled') {
      stopNativePreviewTimer();
      statusNote.value = result.supported && result.frameData
        ? `原生首帧预览已就绪 · ${nativePreviewDecoder.value}`
        : `原生首帧预览不可用 · ${result.message || '请接入 FFmpeg 或其他 native decoder'}`;
    }
  } catch {
    stopNativePreviewTimer();
  } finally { nativePreviewRequesting = false; }
}

async function startConversion() {
  if (converting.value) return;
  stopNativeMediaTimer();
  stopNativeSaveTimer();
  nativeMediaTaskId.value = '';
  nativeMediaOutput.value = '';
  nativeSaveTaskId.value = '';
  nativeSaveName.value = '';
  savingOutput.value = false;
  converting.value = true;
  outputReady.value = false;
  progress.value = 0;
  queue.value[0].state = 'queued';
  statusNote.value = `正在将素材转换为 ${selectedFormat.value} · ${selectedCodec.value}`;
  bridgeProbe.value = props.nativeConnected ? '原生渲染统计已读取' : '演示模式 · 等待原生连接';
  if (!props.nativeClient) {
    converting.value = false;
    statusNote.value = '连接 SOUI 宿主后才能执行真实媒体转换；当前不会用前端进度条伪造输出。';
    bridgeProbe.value = 'native host unavailable';
    return;
  }
  const stats = await props.nativeClient.call('desktop.rendering.getStats', {}).catch(() => undefined);
  if (stats) bridgeProbe.value = `${stats.rendererBackend || stats.gpuStatus || backendLabel.value} · C++ Bridge ready`;
  const nativeSource = nativeFileToken.value || (!userSource.value ? 'sample' : '');
  if (nativeSource) {
    try {
      const started = await props.nativeClient.call('media.convert.start', {
        source: nativeSource, format: selectedFormat.value, codec: selectedCodec.value,
        quality: quality.value, operationId: `media-${nativeSource}-${Date.now()}`,
      });
      nativeMediaTaskId.value = started.taskId;
      nativeMediaOutput.value = started.output;
      bridgeProbe.value = 'native media task · C++ worker';
      statusNote.value = `原生媒体任务已接受 · ${started.output}`;
      nativeMediaTimer = window.setInterval(() => { void pollNativeConversion(); }, 120);
      await pollNativeConversion();
      return;
    } catch (error) {
      converting.value = false;
      statusNote.value = `原生导出不可用 · ${error instanceof Error ? error.message : 'native adapter 未配置'}`;
      bridgeProbe.value = 'native media adapter unavailable';
      return;
    }
  }
  converting.value = false;
  statusNote.value = '浏览器本地文件只完成预览读取；请使用“系统选择并授权文件”后交给 native encoder。';
  bridgeProbe.value = 'native file token required';
}

async function pollNativeConversion() {
  if (!props.nativeClient || !nativeMediaTaskId.value || nativeMediaRequesting) return;
  nativeMediaRequesting = true;
  const taskId = nativeMediaTaskId.value;
  try {
    const result = await props.nativeClient.call('media.convert.query', { taskId });
    if (taskId !== nativeMediaTaskId.value) return;
    progress.value = Math.max(0, Math.min(100, Number(result.progress) || 0));
    statusNote.value = result.message || `原生任务进行中 · ${progress.value}%`;
    if (result.state === 'succeeded') {
      stopNativeMediaTimer();
      converting.value = false;
      outputReady.value = true;
      progress.value = 100;
      statusNote.value = `原生样例导出完成 · ${result.output || nativeMediaOutput.value}`;
      queue.value[0].state = 'done';
    } else if (result.state === 'failed' || result.state === 'cancelled') {
      stopNativeMediaTimer();
      converting.value = false;
      statusNote.value = result.message || '原生媒体任务未完成';
    }
  } catch (error) {
    stopNativeMediaTimer();
    converting.value = false;
    statusNote.value = `读取原生媒体任务失败 · ${error instanceof Error ? error.message : 'Bridge 不可用'}`;
  } finally {
    nativeMediaRequesting = false;
  }
}

async function cancelNativeConversion() {
  if (!props.nativeClient || !nativeMediaTaskId.value) return;
  try {
    await props.nativeClient.call('media.convert.cancel', { taskId: nativeMediaTaskId.value });
    statusNote.value = '已请求取消原生媒体任务';
  } catch (error) {
    statusNote.value = `取消原生媒体任务失败 · ${error instanceof Error ? error.message : 'Bridge 不可用'}`;
  }
}

async function saveNativeOutput() {
  if (!props.nativeClient || !nativeMediaTaskId.value || !outputReady.value || savingOutput.value) return;
  savingOutput.value = true;
  statusNote.value = '正在打开原生保存对话框…';
  try {
    const result = await props.nativeClient.call('desktop.media.saveOutput', {
      taskId: nativeMediaTaskId.value, userGesture: true,
    });
    if (result.cancelled) {
      savingOutput.value = false;
      statusNote.value = '已取消保存输出';
      return;
    }
    nativeSaveTaskId.value = result.taskId;
    nativeSaveName.value = result.name || 'processed';
    statusNote.value = `已选择保存位置 · C++ 正在写入 ${nativeSaveName.value}`;
    stopNativeSaveTimer();
    nativeSaveTimer = window.setInterval(() => { void pollNativeSave(); }, 120);
    await pollNativeSave();
  } catch (error) {
    savingOutput.value = false;
    statusNote.value = `原生保存失败 · ${error instanceof Error ? error.message : 'Bridge 不可用'}`;
  }
}

async function pollNativeSave() {
  if (!props.nativeClient || !nativeSaveTaskId.value || nativeSaveRequesting) return;
  nativeSaveRequesting = true;
  const taskId = nativeSaveTaskId.value;
  try {
    const result = await props.nativeClient.call('media.convert.query', { taskId });
    if (taskId !== nativeSaveTaskId.value) return;
    if (result.state === 'succeeded') {
      stopNativeSaveTimer();
      savingOutput.value = false;
      statusNote.value = `文件已保存 · ${nativeSaveName.value || result.output || '目标路径'}`;
    } else if (result.state === 'failed' || result.state === 'cancelled') {
      stopNativeSaveTimer();
      savingOutput.value = false;
      statusNote.value = result.message || '原生保存任务未完成';
    }
  } catch (error) {
    stopNativeSaveTimer();
    savingOutput.value = false;
    statusNote.value = `读取保存任务失败 · ${error instanceof Error ? error.message : 'Bridge 不可用'}`;
  } finally {
    nativeSaveRequesting = false;
  }
}

function togglePreview() {
  if (videoFallback.value) {
    if (nativePreviewData.value) {
      statusNote.value = `${nativePreviewDecoder.value || '原生'}首帧预览已就绪 · 连续播放需接入 native media adapter`;
      return;
    }
    if (userSource.value) {
      statusNote.value = '当前文件无法在 Miniblink 中解码 · 请接入 native 解码器';
      return;
    }
    previewPlaying.value = !previewPlaying.value;
    statusNote.value = previewPlaying.value ? '兼容帧预览播放中 · 当前内核未提供视频解码器' : '兼容帧预览已暂停';
    return;
  }
  const video = videoElement.value;
  if (!video) return;
  if (video.paused) {
    const playResult = video.play();
    previewPlaying.value = true;
    statusNote.value = '本地视频播放中 · 合成层动画已启用';
    if (playResult && typeof (playResult as Promise<void>).catch === 'function') {
      (playResult as Promise<void>).catch(() => {
        previewPlaying.value = false;
        statusNote.value = '当前 Miniblink 未能解码样例视频';
      });
    }
  } else {
    video.pause();
    previewPlaying.value = false;
    statusNote.value = '视频预览已暂停';
  }
}

function onVideoLoaded() {
  if (videoElement.value) videoDuration.value = videoElement.value.duration || 3;
  if (userSource.value) sourceDetails.value = `${sourceDetails.value.split(' · ')[0]} · ${videoElement.value?.videoWidth || 0}×${videoElement.value?.videoHeight || 0} · 可播放`;
  statusNote.value = '本地视频已加载 · 可播放预览';
}
function onVideoTimeUpdate() {
  if (videoElement.value) videoCurrentTime.value = videoElement.value.currentTime || 0;
}
function onVideoError() {
  videoFallback.value = true;
  previewPlaying.value = !userSource.value;
  statusNote.value = userSource.value ? '当前 Miniblink 无法解码该文件 · 请接入 native 解码器' : '当前内核无视频解码器 · 已切换本地兼容帧预览';
}
function seekVideo(event: Event) {
  const target = event.target as HTMLInputElement;
  const value = Number(target.value);
  if (videoElement.value && Number.isFinite(value)) {
    videoElement.value.currentTime = value;
    videoCurrentTime.value = value;
  }
}

function addQueueItem() {
  if (queue.value.some(item => item.name === '素材_第三条.mp4')) return;
  queue.value.push({ name: '素材_第三条.mp4', detail: '1920×1080 · 01:04', state: 'queued', color: 'amber' });
  statusNote.value = '已加入转换队列';
}

type WindowMethod =
  | 'desktop.window.beginDrag'
  | 'desktop.window.minimize'
  | 'desktop.window.toggleMaximize'
  | 'desktop.window.close';

async function invokeWindowAction(action: string, method: WindowMethod) {
  statusNote.value = `${action}操作已发送 · 原生窗口处理中`;
  if (!props.nativeClient) {
    statusNote.value = `${action}操作仅在连接 SOUI 宿主时生效`;
    return;
  }
  try {
    if (method === 'desktop.window.beginDrag') {
      await props.nativeClient.call(method, { userGesture: true });
    } else if (method === 'desktop.window.minimize') {
      await props.nativeClient.call(method, { userGesture: true });
    } else if (method === 'desktop.window.toggleMaximize') {
      const result = await props.nativeClient.call(method, { userGesture: true });
      statusNote.value = result.maximized ? '窗口已最大化' : '窗口已恢复窗口模式';
      return;
    } else {
      await props.nativeClient.call(method, { userGesture: true });
    }
    statusNote.value = `${action}操作已完成`;
  } catch (error) {
    statusNote.value = `${action}操作未执行 · ${error instanceof Error ? error.message : '原生窗口不可用'}`;
  }
}

function beginNativeDrag(event: MouseEvent) {
  if (event.button !== 0) return;
  void invokeWindowAction('拖动窗口', 'desktop.window.beginDrag');
}

onBeforeUnmount(() => {
  stopNativeMediaTimer();
  stopNativeSaveTimer();
  stopNativePreviewTimer();
  stopWorkloadTimer();
  if (sourceObjectUrl.value && typeof URL.revokeObjectURL === 'function') URL.revokeObjectURL(sourceObjectUrl.value);
});
onMounted(() => {
  const video = videoElement.value;
  if (!video || typeof video.canPlayType !== 'function' || (!video.canPlayType('video/mp4') && !video.canPlayType('video/webm'))) {
    videoFallback.value = true;
    previewPlaying.value = true;
    statusNote.value = '当前内核无视频解码器 · 已切换本地兼容帧预览';
  }
  if (props.nativeClient) {
    void props.nativeClient.call('desktop.workload.getCapabilities', {}).then((result) => {
      workloadCapability.value = result.note;
    }).catch(() => { workloadCapability.value = '原生高负载能力读取失败'; });
    void props.nativeClient.call('desktop.media.getCapabilities', {}).then((result) => {
      nativeMediaCapability.value = result.note;
      if (!userSource.value && result.samplePassthrough) statusNote.value = '原生样例 MP4 导出可用 · 系统选择器支持 MP4 token';
    }).catch(() => { nativeMediaCapability.value = '原生媒体能力读取失败'; });
    if (videoFallback.value) void loadNativePreview('sample');
  }
});
</script>

<template>
  <main class="video-converter" :class="{ 'converter-cpu': !isGpu }" aria-labelledby="converter-title">
    <header class="converter-titlebar">
      <div class="converter-brand" title="拖动窗口" @mousedown.prevent="beginNativeDrag"><span class="converter-logo">▶</span><span><b>迅捷视频转换器</b><small>极速版 · SOUI + MINIBLINK</small></span></div>
      <span class="converter-promo">✦ 领88红包</span>
      <nav class="converter-tabs" aria-label="转换器模块"><button class="active" type="button">转换工作台</button><button type="button" @click="emit('gallery')">组件能力</button><button type="button" @click="emit('scan')">原生文件</button></nav>
      <div class="converter-account"><span>◉ 注册/登录</span><span>◌ 客服中心</span></div>
      <div class="converter-drag-region" title="拖动窗口" aria-label="拖动窗口" @mousedown.prevent="beginNativeDrag"></div>
      <div class="converter-window-actions"><button type="button" aria-label="最小化" @click="invokeWindowAction('最小化', 'desktop.window.minimize')">—</button><button type="button" aria-label="最大化" @click="invokeWindowAction('最大化', 'desktop.window.toggleMaximize')">□</button><button type="button" aria-label="关闭" @click="invokeWindowAction('关闭', 'desktop.window.close')">×</button></div>
    </header>

    <section class="converter-body">
      <aside class="converter-source-column">
        <div class="converter-section-heading"><span class="section-index">01</span><div><p>MEDIA QUEUE</p><h1 id="converter-title">视频格式转换器</h1></div></div>
        <input ref="fileInput" class="file-picker" type="file" accept="video/*,.mov,.mp4,.webm" @change="onFilePicked">
        <button class="drop-zone" type="button" @click="chooseFile" @dragover.prevent @drop.prevent="onDrop"><span class="drop-glyph">＋</span><strong>拖入视频文件</strong><small>支持 MOV / MP4 / WebM · 离线处理</small><em>或点击选择本地文件</em></button>
        <button class="native-file-button" type="button" :disabled="!nativeConnected" @click="chooseNativeFile"><span>▣</span>{{ nativeConnected ? '系统选择并授权文件' : '连接宿主后选择原生文件' }}</button>
        <div class="source-card">
          <div class="source-thumb"><i class="thumb-glow"></i><span>{{ userSource ? 'LOCAL' : 'HD' }}</span><b>{{ formatVideoTime(videoDuration) }}</b></div>
          <div class="source-copy"><strong>{{ sourceName }}</strong><small>{{ sourceDetails }}</small><div class="source-tags"><span>{{ userSource ? 'LOCAL FILE' : 'H.264' }}</span><span>{{ userSource ? 'READY' : 'LOCAL' }}</span></div></div>
          <button class="source-remove" type="button" aria-label="恢复样例文件" @click="resetSource">×</button>
        </div>
        <div class="queue-list">
          <div class="queue-heading"><span>转换队列</span><b>{{ queue.length }} 个任务</b></div>
          <div v-for="item in queue" :key="item.name" class="queue-item" :class="[`queue-${item.color}`, item.state]">
            <span class="queue-dot"></span><div><strong>{{ item.name }}</strong><small>{{ item.detail }}</small></div><em>{{ item.state === 'done' ? '完成' : item.state === 'ready' ? '待转换' : '队列中' }}</em>
          </div>
        </div>
      </aside>

      <section class="converter-preview-column">
        <div class="preview-toolbar"><div><p>LIVE PREVIEW</p><strong>实时预览 / 合成层演示</strong></div><div class="preview-toolbar-right"><span class="backend-pill" :class="{ gpu: isGpu }"><i></i>{{ backendLabel }}</span><button type="button" @click="togglePreview">{{ previewPlaying ? '暂停预览' : '播放预览' }} <b>{{ previewPlaying ? 'Ⅱ' : '▶' }}</b></button></div></div>
        <div class="preview-stage" :class="{ playing: previewPlaying }">
          <video ref="videoElement" v-show="!videoFallback" class="preview-video" preload="metadata" muted loop playsinline @loadedmetadata="onVideoLoaded" @timeupdate="onVideoTimeUpdate" @error="onVideoError"><source v-if="!userSource" :src="sampleMp4Url" type="video/mp4"><source v-if="!userSource" :src="sampleWebmUrl" type="video/webm"></video><img v-if="videoFallback && (nativePreviewData || !userSource)" class="preview-video-fallback" :class="{ 'native-preview': !!nativePreviewData }" :src="nativePreviewData || (previewPlaying ? sampleGifUrl : samplePosterUrl)" alt="本地视频首帧预览"><div v-else-if="videoFallback && userSource" class="video-unavailable"><span>!</span><strong>当前内核无法解码此文件</strong><small>请接入 Media Foundation / FFmpeg native adapter</small></div>
          <div class="preview-grid"></div><div class="preview-noise"></div><div class="preview-scanline"></div>
          <div class="preview-center"><span class="preview-ring ring-a"></span><span class="preview-ring ring-b"></span><div class="preview-core"><b>HD</b><small>MEDIA SIGNAL</small></div></div>
          <div class="preview-caption"><span>PRODUCT FILM / MASTER</span><b>{{ selectedFormat }} · {{ selectedCodec }}</b></div>
          <div class="preview-spectrum"><i v-for="index in 18" :key="index" :style="{ animationDelay: `${index * 35}ms`, height: `${18 + (index % 5) * 8}px` }"></i></div>
          <input class="preview-scrubber" type="range" min="0" :max="videoDuration" step="0.01" :value="videoCurrentTime" aria-label="视频预览进度" @input="seekVideo">
          <div class="preview-time"><span>{{ formatVideoTime(videoCurrentTime) }}</span><b>{{ formatVideoTime(videoDuration) }}</b></div>
        </div>
        <div class="preview-info"><div><span>源分辨率</span><strong>640 × 360</strong></div><div><span>帧率</span><strong>30 fps</strong></div><div><span>色彩空间</span><strong>sRGB / H.264</strong></div><div><span>预计输出</span><strong>{{ outputSize }}</strong></div></div>
        <div class="conversion-progress" :class="{ running: converting, complete: outputReady }"><div class="progress-label"><span>{{ converting ? 'NATIVE MEDIA RUNNING' : outputReady ? 'OUTPUT READY' : 'READY TO CONVERT' }}</span><b>{{ converting || outputReady ? `${progress}%` : '—' }}</b></div><div class="progress-rail"><i :style="{ width: `${outputReady ? 100 : progress}%` }"></i><span v-if="converting" class="progress-spark"></span></div><small>{{ statusNote }}</small><div class="conversion-actions"><button v-if="converting && nativeMediaTaskId" class="conversion-cancel" type="button" @click="cancelNativeConversion">取消原生任务</button><button v-if="outputReady && nativeMediaTaskId" class="conversion-save" type="button" :disabled="savingOutput" @click="saveNativeOutput">{{ savingOutput ? '保存中…' : '保存到本地' }}</button></div></div>
      </section>

      <aside class="converter-settings-column">
        <div class="converter-section-heading compact"><span class="section-index">02</span><div><p>OUTPUT PROFILE</p><h2>输出设置</h2></div></div>
        <div class="setting-block"><label>格式</label><div class="format-switch"><button v-for="format in formats" :key="format" type="button" :class="{ selected: selectedFormat === format }" @click="selectedFormat = format">{{ format }}</button></div></div>
        <div class="setting-block"><label for="codec">视频编码</label><select id="codec" v-model="selectedCodec"><option v-for="codec in codecs" :key="codec">{{ codec }}</option></select></div>
        <div class="setting-block"><div class="setting-label-row"><label for="quality">画质优先级</label><b>{{ quality }}%</b></div><input id="quality" v-model="quality" type="range" min="35" max="100" step="1"><div class="range-hints"><span>更小文件</span><span>更高质量</span></div></div>
        <div class="setting-block"><div class="setting-label-row"><label>保留 HDR 元数据</label><span class="switch-on"><i></i>由 native encoder 决定</span></div><small class="setting-hint">当前编码器会清理元数据；接入专用 HDR profile 后才会实际保留。</small></div>
        <small v-if="nativeMediaCapability" class="native-media-capability">{{ nativeMediaCapability }}</small>
        <button class="workload-launch-button" type="button" :disabled="!nativeConnected || workloadState === 'running' || workloadState === 'cancel-pending'" @click="startWorkloadBenchmark">{{ workloadState === 'running' ? `运行 ${workloadProgress}%` : `运行${activeWorkload.title}压力测试` }}</button>
        <div class="output-card"><div class="output-card-top"><span>OUTPUT PRESET</span><b>{{ selectedFormat }}</b></div><strong>{{ nativeMediaOutput || (selectedFormat === 'WebM' ? 'web_optimized' : 'master_delivery') + '.' + selectedFormat.toLowerCase() }}</strong><small>{{ selectedCodec }} · {{ outputSize }} · {{ eta }} remaining</small></div>
        <KitButton class="convert-button" type="primary" :disabled="converting" @click="startConversion">{{ converting ? '转换进行中…' : outputReady ? '再次转换' : '开始转换' }} <span>↗</span></KitButton>
        <button class="secondary-action" type="button" @click="emit('scan')">打开原生文件管理 <span>→</span></button>
      </aside>
    </section>

    <section class="workload-rail" aria-label="高负载产品能力" :data-4k-sample="sample4kMp4Url">
      <div class="workload-rail-heading"><div><p>HIGH-LOAD CLIENT PROFILES</p><strong>同一套渲染与 Bridge，承载不同桌面产品。</strong></div><small>UI / renderer ready · 专用引擎按需接入</small></div>
      <div class="workload-grid"><button v-for="workload in workloads" :key="workload.id" type="button" class="workload-card" :class="[`workload-${workload.id}`, { selected: selectedWorkload === workload.id }]" @click="selectedWorkload = workload.id"><span class="workload-icon">{{ workload.icon }}</span><span><small>{{ workload.eyebrow }}</small><strong>{{ workload.title }}</strong></span><em>{{ workload.state }}</em></button></div>
      <div class="workload-detail"><span class="workload-detail-icon">{{ activeWorkload.icon }}</span><div><strong>{{ activeWorkload.title }}</strong><p>{{ activeWorkload.detail }}</p><small v-if="workloadCapability">{{ workloadCapability }}</small><small v-if="Object.keys(workloadMetrics).length" class="workload-metrics">{{ Object.entries(workloadMetrics).filter(([key]) => !['kind','note','decoder','geometryKernel','compression','hardwareDecode','zipBombGuard'].includes(key)).slice(0, 3).map(([key, value]) => `${key}: ${typeof value === 'number' ? Number(value).toFixed(1) : value}`).join(' · ') }}</small><small v-if="workloadMetrics.nativeFrameDecoded !== undefined" class="workload-metrics">native frame: {{ workloadMetrics.nativeFrameDecoded ? 'Media Foundation decoded' : 'synthetic fallback' }}</small><small v-if="workloadMetrics.nativeFrames !== undefined" class="workload-metrics">continuous decode: {{ workloadMetrics.nativeFrames }} frames · {{ Number(workloadMetrics.nativeDecodeFps || 0).toFixed(1) }} fps</small></div><b>{{ workloadState === 'running' || workloadState === 'cancel-pending' ? `${workloadProgress}%` : activeWorkload.metric }}</b><button class="workload-run" type="button" :disabled="!nativeConnected || workloadState === 'running' || workloadState === 'cancel-pending'" @click="startWorkloadBenchmark">{{ workloadState === 'running' ? '运行中' : workloadState === 'cancel-pending' ? '取消中' : '运行原生压力' }}</button><button v-if="workloadState === 'running'" class="workload-cancel" type="button" @click="cancelWorkload">取消</button></div>
    </section>

    <footer class="converter-statusbar"><div class="status-left"><span class="status-live"><i></i>{{ nativeConnected ? 'C++ BRIDGE CONNECTED' : 'PREVIEW BRIDGE' }}</span><span>SESSION / LOCAL-OFFLINE</span></div><div class="status-center"><span>渲染 {{ rendererBackend || 'software-skia-gdi' }}</span><span>{{ architecture || 'x86/x64' }}</span><span>Win7+ TARGET</span></div><div class="status-right"><span>{{ bridgeProbe }}</span><b>{{ outputReady ? '100%' : converting ? `${progress}%` : 'IDLE' }}</b></div></footer>
  </main>
</template>

<style scoped>
.video-converter { min-width:0; flex:1; min-height:100vh; padding:24px 30px 78px; color:#e9f3ff; background:#0d1725; overflow:hidden; }
.converter-titlebar,.converter-brand,.converter-brand>span:last-child,.converter-tabs,.converter-window-actions,.converter-body,.converter-section-heading,.preview-toolbar,.preview-toolbar-right,.preview-info,.progress-label,.setting-label-row,.range-hints,.output-card-top,.converter-statusbar,.status-left,.status-center,.status-right,.source-card,.source-copy,.queue-item { display:flex; align-items:center; }
.converter-titlebar { max-width:1420px; height:45px; margin:0 auto; padding-bottom:17px; border-bottom:1px solid #26384c; justify-content:space-between; }
.converter-brand { min-width:226px; }.converter-logo { display:inline-flex; align-items:center; justify-content:center; width:30px; height:30px; margin-right:10px; border:1px solid #79e1c6; border-radius:9px; color:#0d1725; background:#79e1c6; font-weight:800; box-shadow:0 0 16px #79e1c644; }.converter-brand>span:last-child { flex-direction:column; align-items:flex-start; }.converter-brand b { font-size:12px; letter-spacing:.14em; }.converter-brand small { margin-top:3px; color:#728aa2; font-size:8px; letter-spacing:.11em; }
.converter-brand,.converter-drag-region { cursor:move; user-select:none; }
.converter-drag-region { align-self:stretch; flex:1; min-width:28px; }
.converter-promo,.converter-account { display:flex; align-items:center; white-space:nowrap; }
.converter-promo { margin-left:13px; padding:4px 9px; border-radius:999px; color:#fff; background:#ff466d; box-shadow:0 3px 10px #d7355b3d; font-size:9px; font-weight:700; }
.converter-account { margin-left:16px; color:#dff1ff; font-size:9px; }.converter-account span+span { margin-left:12px; }
.converter-tabs { margin-left:auto; margin-right:25px; }.converter-tabs button { padding:6px 12px; border:0; color:#7890a8; background:transparent; cursor:pointer; font-size:10px; letter-spacing:.06em; }.converter-tabs button+button { margin-left:8px; }.converter-tabs button.active,.converter-tabs button:hover { color:#e5f5ff; }.converter-tabs button.active { border-bottom:2px solid #79e1c6; }
.converter-window-actions button { width:29px; height:25px; margin-left:4px; border:1px solid #2f465d; border-radius:5px; color:#91a9be; background:#122336; cursor:pointer; font-size:12px; }.converter-window-actions button:last-child:hover { border-color:#e47f88; color:#ffb3b7; }
.converter-body { max-width:1420px; margin:0 auto; padding:27px 0 21px; align-items:stretch; }.converter-source-column { flex:0 0 28%; min-width:0; margin-right:18px; }.converter-preview-column { flex:1; min-width:0; margin-right:18px; }.converter-settings-column { flex:0 0 26%; min-width:0; }
.converter-section-heading { margin-bottom:17px; }.converter-section-heading .section-index { display:inline-flex; align-items:center; justify-content:center; width:27px; height:27px; margin-right:10px; border:1px solid #37506a; border-radius:7px; color:#79e1c6; font-size:10px; }.converter-section-heading div { min-width:0; }.converter-section-heading p,.preview-toolbar p { margin:0; color:#70d8be; font-size:9px; font-weight:700; letter-spacing:.16em; }.converter-section-heading h1 { margin:5px 0 0; color:#f0f7ff; font-size:23px; font-weight:600; letter-spacing:-.03em; }.converter-section-heading.compact h2 { margin:5px 0 0; color:#f0f7ff; font-size:19px; font-weight:600; }
.drop-zone { display:flex; flex-direction:column; align-items:center; justify-content:center; width:100%; min-height:142px; margin-bottom:12px; border:1px dashed #41637b; border-radius:11px; color:#b9cede; background:#102235; cursor:pointer; transition:border-color 160ms ease, transform 160ms ease, background 160ms ease; }.drop-zone:hover { border-color:#79e1c6; background:#122b3d; transform:translateY(-2px); }.drop-glyph { color:#79e1c6; font-size:25px; font-weight:300; }.drop-zone strong { margin-top:7px; color:#ecf7ff; font-size:12px; }.drop-zone small { margin-top:5px; color:#829ab0; font-size:9px; }.drop-zone em { margin-top:9px; color:#6e879d; font-size:8px; font-style:normal; letter-spacing:.05em; }
.native-file-button { display:flex; align-items:center; justify-content:center; width:100%; margin:-2px 0 12px; padding:7px 8px; border:1px solid #2f5068; border-radius:6px; color:#8db0c5; background:#11283a; cursor:pointer; font-size:9px; }.native-file-button span { margin-right:7px; color:#79e1c6; }.native-file-button:hover { border-color:#79e1c6; color:#dffcf3; }.native-file-button:disabled { cursor:not-allowed; opacity:.6; }
.file-picker { display:none; }
.source-card { position:relative; min-height:79px; padding:10px; border:1px solid #263e54; border-radius:9px; background:#112439; }.source-thumb { position:relative; display:flex; align-items:center; justify-content:center; flex:0 0 75px; height:58px; overflow:hidden; border-radius:6px; color:#e4fff6; background:linear-gradient(135deg,#214966,#247c78); font-size:14px; font-weight:700; }.source-thumb b { position:absolute; right:5px; bottom:4px; color:#bce8df; font-size:7px; font-weight:400; }.thumb-glow { position:absolute; width:36px; height:70px; border-radius:50%; background:#ffffff22; transform:rotate(26deg); animation:converter-shine 3.2s ease-in-out infinite; }.source-copy { min-width:0; flex:1; flex-direction:column; align-items:flex-start; margin-left:10px; }.source-copy strong { overflow:hidden; max-width:100%; color:#eaf5ff; font-size:11px; text-overflow:ellipsis; white-space:nowrap; }.source-copy small { margin-top:4px; color:#89a1b7; font-size:8px; }.source-tags { margin-top:6px; }.source-tags span { margin-right:5px; padding:2px 5px; border-radius:3px; color:#82d9c0; background:#79e1c614; font-size:7px; }.source-remove { align-self:flex-start; border:0; color:#718ba0; background:transparent; cursor:pointer; font-size:16px; }
.queue-list { margin-top:21px; }.queue-heading { display:flex; justify-content:space-between; margin-bottom:9px; color:#a5bdd0; font-size:10px; }.queue-heading b { color:#6e879e; font-size:8px; font-weight:400; }.queue-item { min-height:47px; margin-bottom:7px; padding:8px 10px; border:1px solid #21394f; border-radius:7px; background:#102136; }.queue-dot { width:7px; height:7px; margin-right:9px; border-radius:50%; background:#7890a8; }.queue-item>div { min-width:0; flex:1; }.queue-item strong,.queue-item small { display:block; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }.queue-item strong { color:#cfe0ee; font-size:9px; font-weight:500; }.queue-item small { margin-top:3px; color:#748da5; font-size:8px; }.queue-item em { color:#718ca3; font-size:8px; font-style:normal; }.queue-item.ready .queue-dot { background:#79e1c6; box-shadow:0 0 0 4px #79e1c61c; }.queue-item.done .queue-dot { background:#79e1c6; }.queue-item.done em { color:#79e1c6; }.queue-blue .queue-dot { background:#76a6ff; }.queue-amber .queue-dot { background:#e7b963; }
.preview-toolbar { justify-content:space-between; min-height:34px; margin-bottom:11px; }.preview-toolbar strong { display:block; margin-top:4px; color:#dceaf6; font-size:11px; font-weight:500; }.preview-toolbar-right { margin-left:10px; }.backend-pill { display:inline-flex; align-items:center; margin-right:12px; padding:5px 8px; border:1px solid #304b64; border-radius:999px; color:#91a8bd; font-size:8px; }.backend-pill i { width:5px; height:5px; margin-right:6px; border-radius:50%; background:#e0b35e; }.backend-pill.gpu i { background:#79e1c6; box-shadow:0 0 0 4px #79e1c622; }.preview-toolbar-right button { border:0; color:#79e1c6; background:transparent; cursor:pointer; font-size:9px; }.preview-toolbar-right button b { margin-left:5px; font-weight:400; }
.preview-stage { position:relative; height:315px; overflow:hidden; border:1px solid #294862; border-radius:12px; background:linear-gradient(145deg,#162d43,#0e2335 60%,#113c48); box-shadow:0 15px 35px #0003; }.preview-video,.preview-video-fallback,.video-unavailable { position:absolute; inset:0; z-index:0; width:100%; height:100%; object-fit:cover; opacity:.26; }.preview-video-fallback { opacity:.22; }.preview-video-fallback.native-preview { object-fit:contain; opacity:.72; background:#0f2131; }.video-unavailable { display:flex; flex-direction:column; align-items:center; justify-content:center; color:#9eb6c8; background:#122639; text-align:center; }.video-unavailable span { display:flex; align-items:center; justify-content:center; width:33px; height:33px; margin-bottom:8px; border:1px solid #e6ba68; border-radius:50%; color:#e6ba68; font-size:17px; }.video-unavailable strong { color:#d9e7f1; font-size:11px; font-weight:500; }.video-unavailable small { margin-top:5px; color:#7892a8; font-size:8px; }.preview-grid { position:absolute; z-index:1; inset:0; opacity:.22; background-image:linear-gradient(#9de4d71a 1px,transparent 1px),linear-gradient(90deg,#9de4d71a 1px,transparent 1px); background-size:29px 29px; animation:converter-grid 9s linear infinite; }.preview-noise { position:absolute; z-index:1; inset:0; opacity:.18; background:radial-gradient(ellipse at 70% 35%,#79e1c622 0,transparent 52%),radial-gradient(ellipse at 24% 75%,#6f94ff1c 0,transparent 48%); }.preview-scanline { position:absolute; z-index:2; left:-10%; right:-10%; top:7%; height:2px; opacity:0; background:#b7f7e8aa; box-shadow:0 0 14px #79e1c6; transform:skewY(-3deg); }.preview-stage.playing .preview-scanline { opacity:.65; animation:converter-scan 3.1s ease-in-out infinite; }.preview-center { position:absolute; z-index:3; left:50%; top:48%; width:180px; height:180px; margin:-90px; }.preview-ring { position:absolute; left:50%; top:50%; border:1px solid #79e1c655; border-radius:50%; transform:translate(-50%,-50%); }.ring-a { width:175px; height:175px; animation:converter-ring 4.2s ease-in-out infinite; }.ring-b { width:120px; height:120px; border-color:#7ea6ff66; animation:converter-ring 3.4s .55s ease-in-out infinite reverse; }.preview-core { position:absolute; left:50%; top:50%; display:flex; flex-direction:column; align-items:center; justify-content:center; width:84px; height:84px; margin:-42px; border:1px solid #9cebd7; border-radius:24px; color:#e5fff8; background:linear-gradient(140deg,#39a58e,#255991); box-shadow:0 0 0 10px #79e1c61a,0 0 35px #54c7af66; transform:translateY(0); }.preview-core b { font-size:20px; letter-spacing:.08em; }.preview-core small { margin-top:3px; color:#b9f3e6; font-size:7px; letter-spacing:.12em; }.preview-stage.playing .preview-core { animation:converter-core 2.5s ease-in-out infinite; }.preview-caption { position:absolute; z-index:3; left:20px; top:18px; display:flex; flex-direction:column; }.preview-caption span { color:#75d9bf; font-size:8px; letter-spacing:.14em; }.preview-caption b { margin-top:5px; color:#aec6d8; font-size:9px; font-weight:400; }.preview-spectrum { position:absolute; z-index:3; left:20px; right:20px; bottom:48px; display:flex; align-items:flex-end; height:38px; opacity:.65; }.preview-spectrum i { flex:1; margin-right:3px; border-radius:2px 2px 0 0; background:linear-gradient(#79e1c6,#79e1c622); transform:scaleY(.35); transform-origin:bottom; }.preview-stage.playing .preview-spectrum i { animation:converter-spectrum 1.05s ease-in-out infinite alternate; }.preview-scrubber { position:absolute; z-index:4; left:20px; bottom:21px; width:calc(100% - 40px); height:8px; margin:0; padding:0; border:0; background:transparent; accent-color:#79e1c6; }.preview-time { position:absolute; z-index:4; left:20px; right:20px; bottom:7px; display:flex; justify-content:space-between; color:#89a4ba; font-size:8px; }
.preview-info { margin-top:10px; padding:10px 0; border-bottom:1px solid #223a51; }.preview-info>div { flex:1; min-width:0; }.preview-info span,.preview-info strong { display:block; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }.preview-info span { color:#6f899f; font-size:8px; }.preview-info strong { margin-top:4px; color:#c8d9e7; font-size:10px; font-weight:500; }
.conversion-progress { margin-top:14px; }.progress-label { justify-content:space-between; color:#7e98ae; font-size:8px; letter-spacing:.12em; }.progress-label b { color:#79e1c6; font-size:11px; letter-spacing:0; font-weight:500; }.progress-rail { position:relative; height:5px; margin-top:8px; overflow:hidden; border-radius:3px; background:#1a3045; }.progress-rail i { display:block; height:100%; border-radius:inherit; background:linear-gradient(90deg,#3d9f8d,#79e1c6); transition:width 160ms linear; }.progress-spark { position:absolute; top:0; width:42px; height:5px; background:#e4fff8aa; box-shadow:0 0 13px #d8fff3; animation:converter-spark 1s linear infinite; }.conversion-progress small { display:block; margin-top:7px; color:#718ba2; font-size:9px; }.conversion-progress.complete .progress-label span { color:#79e1c6; }.conversion-actions { display:flex; align-items:center; gap:8px; margin-top:7px; }.conversion-cancel,.conversion-save { padding:4px 8px; border:1px solid #a7cce6; border-radius:999px; color:#2d84d6; background:#f2f9ff; cursor:pointer; font-size:8px; }.conversion-save { border-color:#85cbb9; color:#16866f; background:#edfbf6; }.conversion-save:disabled { cursor:wait; opacity:.6; }.native-media-capability { display:block; margin:-8px 0 13px; color:#7c97ac; font-size:8px; line-height:1.5; }
.setting-block { margin:0 0 19px; }.setting-block label { display:block; margin-bottom:8px; color:#9ab0c2; font-size:9px; letter-spacing:.08em; }.format-switch { display:flex; }.format-switch button { flex:1; height:31px; border:1px solid #2c465e; color:#8da5b9; background:#112337; cursor:pointer; font-size:9px; }.format-switch button:first-child { border-radius:5px 0 0 5px; }.format-switch button:last-child { border-radius:0 5px 5px 0; }.format-switch button+button { border-left:0; }.format-switch button.selected,.format-switch button:hover { color:#102031; background:#79e1c6; border-color:#79e1c6; }.setting-block select { width:100%; height:32px; padding:0 9px; border:1px solid #2c465e; border-radius:5px; color:#cce0ef; background:#112337; font-size:10px; }.setting-label-row { justify-content:space-between; }.setting-label-row label { margin:0; }.setting-label-row>b { color:#79e1c6; font-size:11px; font-weight:500; }.setting-block input[type=range] { width:100%; accent-color:#79e1c6; }.range-hints { justify-content:space-between; margin-top:4px; color:#627e96; font-size:8px; }.switch-on { display:inline-flex; align-items:center; padding:3px 6px; border:1px solid #3a6c6a; border-radius:999px; color:#79e1c6; background:#163b3a; cursor:pointer; font-size:8px; }.switch-on i { width:5px; height:5px; margin-right:5px; border-radius:50%; background:#79e1c6; }.setting-hint { display:block; margin-top:8px; color:#647f96; font-size:8px; }.output-card { margin:8px 0 14px; padding:12px; border:1px solid #31516a; border-radius:8px; background:linear-gradient(135deg,#142d43,#112237); }.output-card-top { justify-content:space-between; color:#78dabe; font-size:8px; letter-spacing:.1em; }.output-card-top b { color:#d8f9ef; font-size:12px; letter-spacing:.04em; }.output-card>strong { display:block; margin-top:13px; overflow:hidden; color:#e4f2fd; font-size:12px; text-overflow:ellipsis; white-space:nowrap; }.output-card>small { display:block; margin-top:5px; color:#7892a8; font-size:8px; }.convert-button { width:100%; justify-content:center; box-shadow:0 8px 24px #3ca88b3b; }.convert-button span { margin-left:8px; }.secondary-action { width:100%; margin-top:8px; padding:8px 0; border:0; color:#7fd9c1; background:transparent; cursor:pointer; text-align:left; font-size:9px; }.secondary-action span { float:right; font-size:14px; }
.converter-statusbar { position:fixed; left:232px; right:0; bottom:0; z-index:20; min-height:35px; padding:10px 30px 11px; border-top:1px solid #26384c; color:#6f899f; background:#0d1725; font-size:8px; letter-spacing:.06em; }.status-left,.status-center,.status-right { flex:1; min-width:0; }.status-center { justify-content:center; }.status-right { justify-content:flex-end; }.status-left span+span,.status-center span+span { margin-left:13px; }.status-live { color:#79e1c6; }.status-live i { display:inline-block; width:5px; height:5px; margin-right:5px; border-radius:50%; background:#79e1c6; box-shadow:0 0 0 4px #79e1c61c; }.status-right b { margin-left:9px; color:#79e1c6; font-size:9px; font-weight:500; }
.workload-rail { max-width:1420px; margin:0 auto 17px; padding:13px 15px 12px; border:1px solid #263e54; border-radius:9px; background:#101f31; }.workload-rail-heading,.workload-rail-heading>div,.workload-grid,.workload-detail { display:flex; align-items:center; }.workload-rail-heading { justify-content:space-between; }.workload-rail-heading p { margin:0; color:#70d8be; font-size:8px; font-weight:700; letter-spacing:.14em; }.workload-rail-heading strong { display:block; margin-top:4px; color:#dbe9f5; font-size:11px; font-weight:500; }.workload-rail-heading small { color:#6f899f; font-size:8px; }.workload-grid { margin-top:10px; }.workload-card { display:flex; align-items:center; flex:1; min-width:0; min-height:43px; margin-right:8px; padding:7px 9px; border:1px solid #253e56; border-radius:6px; color:#9db4c7; background:#122437; cursor:pointer; text-align:left; }.workload-card:last-child { margin-right:0; }.workload-card.selected,.workload-card:hover { border-color:#79e1c6; background:#143044; }.workload-icon { display:inline-flex; align-items:center; justify-content:center; flex:0 0 25px; width:25px; height:25px; margin-right:8px; border-radius:6px; color:#79e1c6; background:#79e1c61c; font-size:14px; }.workload-cad .workload-icon { color:#89b4ff; background:#89b4ff1c; }.workload-zip .workload-icon { color:#e6ba68; background:#e6ba681c; }.workload-card>span:nth-child(2) { min-width:0; }.workload-card small,.workload-card strong { display:block; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }.workload-card small { color:#6f899f; font-size:7px; letter-spacing:.12em; }.workload-card strong { margin-top:3px; color:#d6e6f1; font-size:9px; font-weight:500; }.workload-card em { margin-left:auto; color:#79e1c6; font-size:7px; font-style:normal; white-space:nowrap; }.workload-detail { min-height:31px; margin-top:9px; padding-top:8px; border-top:1px solid #20384d; }.workload-detail-icon { display:inline-flex; align-items:center; justify-content:center; width:22px; height:22px; margin-right:8px; border-radius:5px; color:#79e1c6; background:#79e1c61c; font-size:12px; }.workload-detail div { min-width:0; flex:1; }.workload-detail strong { color:#bbd4e4; font-size:9px; }.workload-detail p { display:inline; margin:0 0 0 8px; color:#718ba2; font-size:8px; }.workload-detail>b { color:#6f94ff; font-size:8px; letter-spacing:.08em; }
@keyframes converter-grid { from { background-position:0 0; } to { background-position:0 29px; } } @keyframes converter-scan { 0% { transform:translateY(0) skewY(-3deg); opacity:0; } 18%,72% { opacity:.7; } 100% { transform:translateY(260px) skewY(-3deg); opacity:0; } } @keyframes converter-ring { 0%,100% { opacity:.35; transform:translate(-50%,-50%) scale(.95); } 50% { opacity:.9; transform:translate(-50%,-50%) scale(1.05); } } @keyframes converter-core { 0%,100% { transform:translateY(0) scale(1); } 50% { transform:translateY(-5px) scale(1.04); } } @keyframes converter-spectrum { from { transform:scaleY(.3); opacity:.5; } to { transform:scaleY(1); opacity:1; } } @keyframes converter-spark { from { transform:translateX(-50px); } to { transform:translateX(420px); } } @keyframes converter-shine { 0%,100% { opacity:.05; transform:translateX(-26px) rotate(26deg); } 50% { opacity:.28; transform:translateX(26px) rotate(26deg); } }
.converter-cpu .preview-video,.converter-cpu .preview-video-fallback { opacity:.18; }.converter-cpu .preview-grid { animation:none; opacity:.08; }.converter-cpu .preview-scanline,.converter-cpu .preview-spectrum,.converter-cpu .thumb-glow { animation:none !important; opacity:.35; }.converter-cpu .preview-stage.playing .preview-core { animation:none; }.converter-cpu .preview-ring { animation:none; opacity:.5; }.converter-cpu .progress-spark { display:none; }
@media (prefers-reduced-motion: reduce) { .video-converter *, .video-converter *::before, .video-converter *::after { animation:none !important; transition-duration:0ms !important; } }
@media (max-width:1120px) { .video-converter { padding:20px 20px 78px; }.converter-statusbar { left:200px; padding-left:20px; padding-right:20px; }.converter-body { flex-wrap:wrap; }.converter-source-column { flex-basis:35%; }.converter-preview-column { flex-basis:calc(65% - 18px); margin-right:0; }.converter-settings-column { flex-basis:100%; margin-top:22px; }.converter-settings-column .setting-block { display:inline-block; width:31%; margin-right:2%; vertical-align:top; }.converter-settings-column .output-card { display:inline-block; width:31%; margin-right:2%; vertical-align:top; }.converter-settings-column .convert-button,.converter-settings-column .secondary-action { width:31%; margin-right:2%; vertical-align:top; }.converter-settings-column .secondary-action { margin-top:16px; }.workload-rail-heading small { display:none; } }
@media (max-width:720px) { .video-converter { padding:16px 13px 28px; overflow:auto; }.converter-titlebar { height:auto; padding-bottom:12px; flex-wrap:wrap; }.converter-tabs { order:3; width:100%; margin:12px 0 0; justify-content:flex-start; }.converter-window-actions { margin-left:auto; }.converter-body { display:block; }.converter-source-column,.converter-preview-column { margin:0 0 21px; }.converter-settings-column .setting-block,.converter-settings-column .output-card,.converter-settings-column .convert-button,.converter-settings-column .secondary-action { display:block; width:100%; margin-right:0; }.converter-preview-column { overflow:hidden; }.preview-stage { height:250px; }.converter-statusbar { position:static; display:block; line-height:1.8; padding:9px 0 0; }.status-left,.status-center,.status-right { display:flex; justify-content:flex-start; margin-top:5px; }.status-right { justify-content:space-between; }.converter-brand b { font-size:10px; }.workload-grid { display:block; }.workload-card { margin:0 0 6px; }.workload-card:last-child { margin-bottom:0; }.workload-detail { align-items:flex-start; }.workload-detail p { display:block; margin:4px 0 0; }.workload-detail>b { align-self:center; } }

/* Reference palette: light blue utility workspace with a blue application bar
   and white cards. All motion declarations above remain unchanged. */
.video-converter { padding:60px 20px 78px; color:#354a63; background:#edf5ff; }
.converter-titlebar { position:fixed; left:0; right:0; top:0; z-index:40; max-width:none; height:44px; padding:0 16px; border:0; color:#fff; background:linear-gradient(90deg,#087af2,#1594f4); box-shadow:0 2px 10px #236db52b; }
.converter-drag-region { cursor:move; }
.converter-promo { margin-left:13px; color:#fff; background:linear-gradient(90deg,#ff7a4a,#ff3f70); }
.converter-account { color:#e4f3ff; }
.converter-logo { width:28px; height:28px; border:1px solid #fff; color:#1778e1; background:#fff; box-shadow:none; }.converter-brand b { color:#fff; font-size:12px; }.converter-brand small { color:#cde8ff; }.converter-tabs button { color:#d9eeff; }.converter-tabs button.active,.converter-tabs button:hover { color:#fff; }.converter-tabs button.active { border-bottom-color:#fff; }.converter-window-actions button { border-color:#70b6f4; color:#e8f5ff; background:#0877e966; }.converter-window-actions button:last-child:hover { border-color:#fff; color:#fff; }
.converter-section-heading .section-index { border-color:#bed7f1; color:#247be2; background:#fff; }.converter-section-heading p,.preview-toolbar p,.workload-rail-heading p { color:#5687b8; }.converter-section-heading h1,.converter-section-heading.compact h2 { color:#273d58; }
.drop-zone { border-color:#75b7f5; color:#55718d; background:#fff; box-shadow:0 4px 14px #477aaa12; }.drop-zone:hover { border-color:#187ff1; color:#315e8a; background:#f8fcff; }.drop-zone strong { color:#2c4c6b; }.drop-zone small { color:#7f98b0; }.drop-zone em { color:#7197ba; }
.native-file-button { border-color:#bad8ee; color:#4c80aa; background:#f5fbff; }.native-file-button span { color:#2385dd; }.native-file-button:hover { border-color:#3a99eb; color:#1f76c7; }
.source-card,.queue-item { border-color:#d2e3f3; background:#fff; box-shadow:0 3px 12px #477aaa10; }.source-thumb { background:linear-gradient(135deg,#1a8df5,#55c9ed); }.source-copy strong { color:#304c68; }.source-copy small,.queue-item small { color:#8198ad; }.source-tags span { color:#3388da; background:#e8f4ff; }.source-remove { color:#90a7ba; }.queue-heading { color:#496985; }.queue-heading b { color:#7894ac; }.queue-item strong { color:#3c5872; }.queue-item em { color:#7792aa; }
.preview-toolbar strong { color:#395a78; }.backend-pill { border-color:#c4dcf0; color:#55738f; background:#fff; }.preview-toolbar-right button { color:#147ee9; }
.preview-stage { border-color:#7abcf0; background:linear-gradient(145deg,#1687f4,#4bc4ec 75%,#83d8ef); box-shadow:0 10px 24px #2f83c52b; }.preview-video,.preview-video-fallback { opacity:.34; }.preview-video-fallback { opacity:.46; }.preview-grid { opacity:.2; background-image:linear-gradient(#ffffff38 1px,transparent 1px),linear-gradient(90deg,#ffffff38 1px,transparent 1px); }.preview-noise { opacity:.12; }.preview-ring { border-color:#ffffff99; }.ring-b { border-color:#e8f8ffcc; }.preview-core { border-color:#fff; color:#fff; background:linear-gradient(140deg,#18aee2,#3473df); box-shadow:0 0 0 10px #ffffff22,0 0 32px #fff6; }.preview-core small { color:#e8f8ff; }.preview-caption span { color:#eaffff; }.preview-caption b,.preview-time { color:#e5f8ff; }.preview-spectrum i { background:linear-gradient(#fff,#ffffff44); }.preview-scrubber { accent-color:#fff; }.preview-info { border-bottom-color:#c9dff1; }.preview-info span { color:#6e8ca7; }.preview-info strong { color:#3e5d78; }.conversion-progress .progress-label { color:#6686a2; }.progress-rail { background:#d4e7f6; }.progress-rail i { background:linear-gradient(90deg,#2688ed,#50c9dc); }.conversion-progress small { color:#718ca7; }
.converter-settings-column { padding:18px; border:1px solid #d2e3f3; border-radius:11px; background:#fff; box-shadow:0 4px 15px #477aaa12; }.format-switch button,.setting-block select { border-color:#c6dced; color:#55718b; background:#f7fbff; }.format-switch button.selected,.format-switch button:hover { color:#fff; background:#2c8ef0; border-color:#2c8ef0; }.setting-block label { color:#55738e; }.setting-label-row>b { color:#2582e6; }.setting-block input[type=range] { accent-color:#2b8ef0; }.range-hints { color:#7b96ad; }.switch-on { border-color:#9fcde8; color:#1782df; background:#edf8ff; }.switch-on i { background:#2b9bd7; }.setting-hint { color:#7c97ac; }.output-card { border-color:#c7dff1; background:linear-gradient(135deg,#f5fbff,#edf6ff); }.output-card-top { color:#4c8bc2; }.output-card-top b,.output-card>strong { color:#355674; }.output-card>small { color:#7895ac; }.convert-button { box-shadow:0 8px 18px #338de63b; }.secondary-action { color:#1d80df; }
.workload-rail { border-color:#d2e3f3; background:#fff; box-shadow:0 3px 14px #477aaa0d; }.workload-rail-heading strong { color:#355572; }.workload-rail-heading small { color:#7d96ad; }.workload-card { border-color:#d4e4f2; color:#53728d; background:#f8fbff; }.workload-card.selected,.workload-card:hover { border-color:#55a9ef; background:#eef8ff; }.workload-card strong { color:#385773; }.workload-card small { color:#829bb0; }.workload-card em { color:#2683df; }.workload-detail { border-top-color:#d6e5f2; }.workload-detail-icon { color:#2686e4; background:#eaf5ff; }.workload-detail strong { color:#46637c; }.workload-detail p { color:#7893aa; }.workload-detail>b { color:#2a82df; }
.workload-detail small { display:block; margin-top:3px; color:#829bb0; font-size:7px; line-height:1.4; }.workload-detail .workload-metrics { color:#2386db; }.workload-detail>b { white-space:nowrap; }.workload-run,.workload-cancel { margin-left:8px; padding:6px 9px; border:1px solid #b8d1e5; border-radius:5px; color:#3d6a8b; background:#f6fbff; cursor:pointer; font-size:8px; white-space:nowrap; }.workload-run:hover,.workload-cancel:hover { border-color:#2683df; color:#2683df; }.workload-run:disabled { cursor:not-allowed; opacity:.55; }.workload-cancel { color:#c55e5e; border-color:#e1b3b3; }
.workload-launch-button { width:100%; margin:0 0 12px; padding:8px 10px; border:1px solid #9bc6e6; border-radius:6px; color:#287ac0; background:#f4faff; cursor:pointer; font-size:10px; font-weight:600; }.workload-launch-button:hover { border-color:#2c8cde; color:#176dad; background:#eaf6ff; }.workload-launch-button:disabled { opacity:.55; cursor:not-allowed; }
.converter-statusbar { left:160px; right:0; padding:10px 30px 11px; border-top-color:#cbdfee; color:#6c879f; background:#f8fbff; }.status-live { color:#1681dd; }.status-live i { background:#39b187; }.status-right b { color:#2583df; }.converter-cpu .preview-video,.converter-cpu .preview-video-fallback { opacity:.28; }
@media (max-width:1100px) { .video-converter { padding:54px 20px 78px; }.converter-statusbar { left:160px; padding-left:20px; padding-right:20px; } }
@media (max-width:720px) { .video-converter { padding:54px 13px 28px; }.converter-statusbar { left:0; right:0; padding:9px 0 0; background:#edf5ff; } }
@media (max-width:720px) { .converter-promo,.converter-account { display:none; } }
/* Native formats are a capability matrix rather than a single three-way toggle. */
.format-switch { flex-wrap:wrap; gap:6px; }
.format-switch button { flex:1 0 calc(33.333% - 4px); border-left:1px solid #c6dced; border-radius:5px !important; }
</style>
