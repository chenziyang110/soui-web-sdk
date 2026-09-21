<script setup lang="ts">
import { computed, ref, watch, onBeforeUnmount, onMounted } from 'vue';
import { KitButton } from './kit-ui';
import type { SouiNativeClientImpl } from '../../../packages/sdk/soui_native_client';

const props = defineProps<{
  nativeConnected: boolean;
  nativeClient?: SouiNativeClientImpl | null;
  rendererBackend?: string;
  architecture?: string;
  active?: boolean;
}>();
const emit = defineEmits<{
  (event: 'scan'): void;
  (event: 'gallery'): void;
}>();

const selectedModule = ref('automation');
const commandRunning = ref(false);
const commandStatus = ref('');
const commandError = ref('');
const insightMetric = ref('—');
const insightMetricLabel = ref('等待 native snapshot');
const tourRunning = ref(false);
let tourTimer: number | undefined;
const isGpu = computed(() => (props.rendererBackend || '').indexOf('gpu') === 0);
const rendererLabel = computed(() => isGpu.value ? 'GPU compositor ready' : 'CPU-safe compositor');
const rendererCaption = computed(() => isGpu.value ? 'WGL / FBO direct present' : 'Skia raster / dirty copy');
const modules = [
  { id: 'automation', eyebrow: 'AUTOMATION', title: '自动化工作流', detail: '把重复的桌面任务交给可靠的原生服务。', accent: 'mint', icon: '↗' },
  { id: 'insight', eyebrow: 'INSIGHT', title: '实时洞察', detail: '以清晰的指标和状态，让复杂任务变得可理解。', accent: 'blue', icon: '◌' },
  { id: 'security', eyebrow: 'SECURITY', title: '安全边界', detail: '权限、会话和离线资源都能被追踪与审计。', accent: 'amber', icon: '◇' },
];
type RuntimeRow = { label: string; detail: string; state: string; tone: 'live' | 'compat' | 'pending' | 'fallback' };
const runtimeRows = ref<RuntimeRow[]>([
  { label: 'GPU compositor', detail: '等待真实呈现状态', state: 'PROBING', tone: 'pending' },
  { label: 'Media Foundation preview', detail: '等待 native capability', state: 'PROBING', tone: 'pending' },
  { label: '4K frame worker', detail: '等待高负载能力', state: 'PROBING', tone: 'pending' },
  { label: 'CAD scene worker', detail: '等待高负载能力', state: 'PROBING', tone: 'pending' },
  { label: 'ZIP integrity worker', detail: '等待高负载能力', state: 'PROBING', tone: 'pending' },
  { label: 'Modern Web / offline', detail: 'Vue + TypeScript + legacy bundle', state: 'LOCAL', tone: 'compat' },
]);
let runtimeProbeTimer: number | undefined;
async function refreshRuntimeBoard() {
  const next = [...runtimeRows.value];
  const gpuIndex = 0;
  const mediaIndex = 1;
  const workloadIndexes = { video4kSynthetic: 2, cadScene: 3, zipStored: 4 };
  if (props.rendererBackend) {
    const gpu = props.rendererBackend.indexOf('gpu-direct') === 0;
    const gpuReadback = props.rendererBackend.indexOf('gpu-readback') === 0;
    next[gpuIndex] = { label: 'GPU compositor', detail: gpu ? 'WGL / FBO direct present' : gpuReadback ? 'WGL / FBO + bounded readback' : 'Skia raster / dirty copy', state: gpu ? 'DIRECT' : gpuReadback ? 'READBACK' : 'CPU FALLBACK', tone: gpu || gpuReadback ? 'live' : 'fallback' };
  }
  if (props.nativeClient) {
    const [stats, media, workload] = await Promise.all([
      props.nativeClient.call('desktop.rendering.getStats', {}).catch(() => undefined),
      props.nativeClient.call('desktop.media.getCapabilities', {}).catch(() => undefined),
      props.nativeClient.call('desktop.workload.getCapabilities', {}).catch(() => undefined),
    ]);
    if (stats) {
      insightMetric.value = `${Math.round(stats.activePacingHz)}`;
      insightMetricLabel.value = `active pacing · paint ${stats.paintCount}`;
    }
    if (media) next[mediaIndex] = { label: 'Media Foundation preview', detail: media.note, state: media.nativeDecoder ? 'NATIVE' : 'PASSTHROUGH', tone: media.nativeDecoder ? 'live' : 'compat' };
    if (workload) {
      for (const [key, index] of Object.entries(workloadIndexes)) {
        const enabled = Boolean((workload as Record<string, unknown>)[key]);
        next[index] = { label: next[index].label, detail: enabled ? 'C++ worker / bounded result' : 'Adapter not configured', state: enabled ? 'READY' : 'ADAPTER', tone: enabled ? 'live' : 'fallback' };
      }
    }
  }
  runtimeRows.value = next;
}
const activeModule = computed(() => modules.find(item => item.id === selectedModule.value) || modules[0]);
function stopTour() {
  tourRunning.value = false;
  if (tourTimer !== undefined) window.clearInterval(tourTimer);
  tourTimer = undefined;
}
function toggleTour() {
  if (props.active === false) return;
  if (tourRunning.value) { stopTour(); return; }
  tourRunning.value = true;
  let index = Math.max(0, modules.findIndex(item => item.id === selectedModule.value));
  tourTimer = window.setInterval(() => {
    index = (index + 1) % modules.length;
    selectedModule.value = modules[index].id;
  }, 2800);
}
watch(() => props.active, (active) => { if (active === false) stopTour(); });
watch(selectedModule, () => {
  commandStatus.value = '';
  commandError.value = '';
});
async function runCommand() {
  if (commandRunning.value) return;
  if (selectedModule.value === 'automation') {
    emit('scan');
    return;
  }
  commandStatus.value = '';
  commandError.value = '';
  if (!props.nativeClient) {
    commandError.value = '请在桌面 Demo 中连接原生宿主后再运行。';
    return;
  }
  commandRunning.value = true;
  try {
    if (selectedModule.value === 'insight') {
      const [stats, media, workload] = await Promise.all([
        props.nativeClient.call('desktop.rendering.getStats', {}),
        props.nativeClient.call('desktop.media.getCapabilities', {}),
        props.nativeClient.call('desktop.workload.getCapabilities', {}),
      ]);
      insightMetric.value = `${Math.round(stats.activePacingHz)}`;
      insightMetricLabel.value = `active pacing · paint ${stats.paintCount}`;
      commandStatus.value = `${stats.gpuStatus} · ${Math.round(stats.activePacingHz)}Hz · ${media.nativeDecoder ? 'MF preview ready' : 'media fallback'} · ${workload.cadScene ? 'CAD worker ready' : 'CAD adapter unavailable'}`;
    } else {
      const result = await props.nativeClient.call('bridge.probe.report', {
        script: true,
        json: true,
        event: true,
        errorHandling: true,
        localResource: true,
        proxy: true,
        promise: true,
        symbol: true,
        reflect: true,
        vueMount: true,
        reactivity: true,
        domEvent: true,
        bridge: true,
      });
      if (!result.accepted) throw new Error('原生能力探针未接受当前会话。');
      commandStatus.value = 'bridge.probe.report accepted · session / origin / token verified';
    }
  } catch (error) {
    commandError.value = error instanceof Error ? error.message : '原生能力读取失败。';
  } finally {
    commandRunning.value = false;
  }
}
onBeforeUnmount(() => {
  stopTour();
  if (runtimeProbeTimer !== undefined) window.clearInterval(runtimeProbeTimer);
  runtimeProbeTimer = undefined;
});
onMounted(() => { void refreshRuntimeBoard(); runtimeProbeTimer = window.setInterval(() => { void refreshRuntimeBoard(); }, 2000); });
</script>

<template>
  <main class="product-showcase" :class="{ 'showcase-cpu': !isGpu }" aria-labelledby="product-showcase-title">
    <header class="showcase-topbar">
      <div class="showcase-kicker"><span class="showcase-mark">S</span><span>SOUI WEB KIT / PRODUCT LAB</span></div>
      <div class="showcase-runtime"><span class="runtime-dot" :class="{ live: nativeConnected }"></span>{{ nativeConnected ? 'Native bridge connected' : 'Preview mode' }} <b>{{ architecture || 'x86/x64' }}</b></div>
    </header>

    <section class="showcase-hero">
      <div class="hero-copy">
        <p class="showcase-eyebrow">DESKTOP PRODUCTS, BUILT TO FEEL ALIVE</p>
        <h1 id="product-showcase-title">把复杂的桌面工具，<em>变成有质感的体验。</em></h1>
        <p class="hero-lede">SOUI 的窗口能力、Miniblink 的 Web UI 和标准 C++ Bridge，在同一套离线脚手架里协同工作。</p>
        <div class="hero-actions">
          <KitButton type="primary" @click="emit('scan')">打开真实扫描</KitButton>
          <button class="showcase-link-button" type="button" @click="emit('gallery')">浏览 82 个组件 <span>→</span></button>
          <button class="showcase-tour" type="button" :aria-pressed="tourRunning" @click="toggleTour"><i :class="{ running: tourRunning }"></i>{{ tourRunning ? '暂停产品演示' : '播放产品演示' }}</button>
        </div>
        <div class="hero-proof"><span>✓</span> 真实 C++ 能力 <span>✓</span> 离线交付 <span>✓</span> Win7+ 目标</div>
      </div>
      <div class="hero-visual" aria-label="渲染能力预览">
        <div class="visual-noise"></div><div class="visual-ring visual-ring-one"></div><div class="visual-ring visual-ring-two"></div>
        <div class="visual-core"><span class="core-shine"></span><strong>{{ isGpu ? 'GPU' : 'CPU' }}</strong><small>RENDER MODE</small></div>
        <div class="visual-orbit orbit-one"><i></i></div><div class="visual-orbit orbit-two"><i></i></div>
        <div class="visual-readout"><span>{{ rendererLabel }}</span><b>{{ rendererCaption }}</b></div>
      </div>
    </section>

    <section class="showcase-metrics" aria-label="能力指标">
      <article class="metric-card"><span class="metric-icon mint">↗</span><div><strong>88/88</strong><small>Miniblink API probes</small></div><span class="metric-state">PASS</span></article>
      <article class="metric-card"><span class="metric-icon blue">◈</span><div><strong>82</strong><small>Element Plus component families</small></div><span class="metric-state">LOCAL</span></article>
      <article class="metric-card"><span class="metric-icon amber">◎</span><div><strong>375</strong><small>offline delivery assets</small></div><span class="metric-state">READY</span></article>
      <article class="metric-card metric-card-highlight"><span class="metric-icon violet">⌁</span><div><strong>{{ rendererBackend || 'software' }}</strong><small>active renderer backend</small></div><span class="metric-state">LIVE</span></article>
    </section>

    <section class="runtime-board" aria-labelledby="runtime-board-title">
      <div class="runtime-board-heading"><div><p class="showcase-eyebrow">LIVE COMPATIBILITY MATRIX</p><h2 id="runtime-board-title">能力从探针到产品路径，实时可见。</h2></div><span>{{ nativeConnected ? 'BRIDGE / LOCAL' : 'PREVIEW / LOCAL' }}</span></div>
      <div class="runtime-board-grid"><article v-for="row in runtimeRows" :key="row.label" class="runtime-board-item"><i :class="`runtime-dot-${row.tone}`"></i><div><strong>{{ row.label }}</strong><small>{{ row.detail }}</small></div><b :class="`runtime-state-${row.tone}`">{{ row.state }}</b></article></div>
    </section>

    <section class="showcase-workbench" aria-labelledby="workbench-title">
      <div class="workbench-heading"><div><p class="showcase-eyebrow">CLIENT TOOLKIT</p><h2 id="workbench-title">一套框架，三种商业产品能力。</h2></div><span class="workbench-caption">SELECT A MODULE / INTERACT WITH THE SYSTEM</span></div>
      <div class="module-layout">
        <nav class="module-list" aria-label="产品能力模块">
          <button v-for="module in modules" :key="module.id" type="button" class="module-item" :class="[`module-${module.accent}`, { selected: selectedModule === module.id }]" @click="selectedModule = module.id">
            <span class="module-icon">{{ module.icon }}</span><span><small>{{ module.eyebrow }}</small><strong>{{ module.title }}</strong></span><b>↗</b>
          </button>
        </nav>
        <article class="module-detail" :class="`detail-${activeModule.accent}`">
          <div class="detail-head"><span class="detail-badge">{{ activeModule.eyebrow }}</span><span class="detail-live"><i></i> ready</span></div>
          <h3>{{ activeModule.title }}</h3><p>{{ activeModule.detail }}</p>
          <div v-if="selectedModule === 'automation'" class="automation-preview">
            <div class="automation-line"><span class="line-dot done"></span><span>选择授权目录</span><b>完成</b></div>
            <div class="automation-line"><span class="line-dot active"></span><span>原生扫描任务</span><b>运行中</b></div>
            <div class="automation-line"><span class="line-dot"></span><span>分页展示结果</span><b>下一步</b></div>
          </div>
          <div v-else-if="selectedModule === 'insight'" class="insight-preview"><div class="insight-number"><strong>{{ insightMetric }}</strong><span>{{ insightMetricLabel }}</span></div><svg viewBox="0 0 240 64" role="img" aria-label="渲染状态趋势预览"><polyline points="0,52 30,44 60,47 90,28 120,34 150,18 180,25 210,8 240,14" fill="none" stroke="currentColor" stroke-width="3" /></svg></div>
          <div v-else class="security-preview"><span class="security-shield">✓</span><div><strong>会话与权限已隔离</strong><small>所有原生调用带 session / origin / token</small></div></div>
          <div class="detail-feedback" aria-live="polite"><span v-if="commandRunning" class="detail-feedback-pending">正在读取 native 状态…</span><span v-else-if="commandError" class="detail-feedback-error">{{ commandError }}</span><span v-else-if="commandStatus" class="detail-feedback-success">{{ commandStatus }}</span><span v-else>点击运行演示读取真实 native 返回</span></div>
          <div class="detail-footer"><span>标准 SDK / C++ Bridge</span><button type="button" @click="runCommand">{{ commandRunning ? '正在响应…' : '运行演示' }} <b>→</b></button></div>
        </article>
      </div>
    </section>

    <section class="showcase-bottom">
      <div class="bottom-quote"><p class="showcase-eyebrow">WHY THIS KIT</p><blockquote>“不是把网页塞进窗口，而是让 Web 成为桌面产品的表达层。”</blockquote><span>— SOUI + Miniblink AI DevKit</span></div>
      <div class="bottom-actions"><button type="button" @click="emit('scan')"><span>01</span><strong>真实原生任务</strong><small>打开只读目录扫描</small><b>↗</b></button><button type="button" @click="emit('gallery')"><span>02</span><strong>完整组件库</strong><small>浏览交互组件与状态</small><b>↗</b></button></div>
    </section>
  </main>
</template>

<style scoped>
.product-showcase { min-width: 0; flex: 1; padding: 28px 42px 44px; color: #23344d; background: #f5f8fc; overflow: hidden; }
.showcase-topbar,.showcase-kicker,.showcase-runtime,.hero-actions,.hero-proof,.workbench-heading,.detail-head,.detail-footer,.showcase-bottom,.bottom-actions,.metric-card,.module-item,.automation-line,.security-preview { display:flex; align-items:center; }
.showcase-topbar { justify-content:space-between; max-width:1180px; margin:0 auto 26px; color:#71829a; font-size:10px; letter-spacing:.12em; }
.showcase-kicker { font-weight:700; }.showcase-mark { display:inline-flex; align-items:center; justify-content:center; width:25px; height:25px; margin-right:9px; border-radius:7px; color:#fff; background:#2f6fed; font-size:13px; letter-spacing:0; }.showcase-runtime { letter-spacing:.02em; }.showcase-runtime b { margin-left:8px; color:#2f6fed; font-weight:600; }.runtime-dot { width:6px; height:6px; margin-right:7px; border-radius:50%; background:#d6a847; }.runtime-dot.live { background:#55b89a; box-shadow:0 0 0 4px #55b89a22; }
.showcase-hero { position:relative; display:flex; max-width:1180px; min-height:350px; margin:0 auto 22px; padding:42px 48px; overflow:hidden; border-radius:18px; color:#e9f4ff; background:linear-gradient(118deg,#17283e,#213b56 62%,#1a5261); box-shadow:0 18px 45px #19365324; }
.hero-copy { position:relative; z-index:2; width:55%; }.showcase-eyebrow { margin:0; color:#72d9bf; font-size:10px; font-weight:700; letter-spacing:.17em; }.hero-copy h1 { max-width:560px; margin:16px 0 14px; font-size:39px; line-height:1.16; letter-spacing:-.04em; font-weight:650; }.hero-copy h1 em { display:block; color:#7ee0c5; font-style:normal; }.hero-lede { max-width:480px; margin:0; color:#b8cbe0; font-size:13px; line-height:1.8; }.hero-actions { margin-top:26px; }.showcase-link-button { margin-left:16px; padding:9px 0; border:0; color:#dcecff; background:transparent; cursor:pointer; font-size:12px; }.showcase-link-button span { margin-left:6px; color:#72d9bf; }.showcase-tour { margin-left:18px; padding:8px 11px; border:1px solid #5e7e96; border-radius:999px; color:#cde4f2; background:#ffffff0a; cursor:pointer; font-size:10px; }.showcase-tour i { display:inline-block; width:6px; height:6px; margin-right:6px; border-radius:50%; background:#8ca8be; }.showcase-tour i.running { background:#72d9bf; box-shadow:0 0 0 4px #72d9bf22; animation:showcase-blink 1.2s ease-in-out infinite; }.hero-proof { margin-top:28px; color:#8ea8c2; font-size:10px; letter-spacing:.02em; }.hero-proof span { margin-right:4px; color:#72d9bf; }.hero-proof span:not(:first-child) { margin-left:14px; }
.hero-visual { position:absolute; right:4%; top:6%; width:39%; height:88%; overflow:hidden; }.visual-noise { position:absolute; inset:0; opacity:.24; background-image:linear-gradient(#b7e7df13 1px,transparent 1px),linear-gradient(90deg,#b7e7df13 1px,transparent 1px); background-size:28px 28px; animation:showcase-grid 9s linear infinite; }.visual-ring { position:absolute; left:50%; top:50%; border:1px solid #72d9bf55; border-radius:50%; transform:translate(-50%,-50%); animation:showcase-ring 5s ease-in-out infinite; }.visual-ring-one { width:260px; height:260px; }.visual-ring-two { width:180px; height:180px; border-color:#8cb7ff55; animation-delay:.7s; animation-duration:4s; }.visual-core { position:absolute; left:50%; top:50%; display:flex; flex-direction:column; align-items:center; justify-content:center; width:102px; height:102px; margin:-51px; border:1px solid #9df2db; border-radius:28px; color:#dffcf4; background:linear-gradient(140deg,#47b69c,#24599a); box-shadow:0 0 0 12px #79e0c522,0 0 45px #59cbb388; transform:none; animation:showcase-core-safe 3.4s ease-in-out infinite; }.visual-core strong { font-size:19px; letter-spacing:.08em; }.visual-core small { margin-top:3px; color:#b7f5e4; font-size:7px; letter-spacing:.14em; }.core-shine { position:absolute; width:40px; height:150px; border-radius:50%; background:#fff5; transform:none; animation:showcase-shine-safe 3.4s ease-in-out infinite; }.visual-orbit { position:absolute; left:50%; top:50%; border:1px solid #8fe6cb66; border-radius:50%; transform:translate(-50%,-50%); animation:showcase-orbit-safe 7s ease-in-out infinite; }.visual-orbit i { position:absolute; left:50%; top:-4px; width:8px; height:8px; margin-left:-4px; border-radius:50%; background:#8fe6cb; box-shadow:0 0 15px #8fe6cb; }.orbit-one { width:300px; height:116px; }.orbit-two { width:220px; height:220px; transform:translate(-50%,-50%); animation-duration:8.5s; animation-delay:.7s; border-color:#7da9fa55; }.orbit-two i { top:auto; bottom:-4px; background:#7da9fa; box-shadow:0 0 15px #7da9fa; }.visual-readout { position:absolute; left:50%; bottom:10px; display:flex; flex-direction:column; width:220px; margin-left:-110px; text-align:center; }.visual-readout span { color:#d9ffef; font-size:10px; letter-spacing:.11em; }.visual-readout b { margin-top:4px; color:#89a7c2; font-size:9px; font-weight:400; letter-spacing:.04em; }
.showcase-metrics { display:flex; max-width:1180px; margin:0 auto 26px; }.metric-card { flex:1; min-width:0; margin-right:12px; padding:16px 15px; border:1px solid #dee7f0; border-radius:10px; background:#fff; box-shadow:0 3px 13px #3c5c7c0c; }.metric-card:last-child { margin-right:0; }.metric-icon { display:inline-flex; align-items:center; justify-content:center; width:28px; height:28px; margin-right:10px; border-radius:8px; font-size:16px; }.metric-icon.mint { color:#238c76; background:#dff8ef; }.metric-icon.blue { color:#3b6fd1; background:#e5efff; }.metric-icon.amber { color:#ac7d2c; background:#fff3dc; }.metric-icon.violet { color:#7454b6; background:#f0e8ff; }.metric-card div { min-width:0; }.metric-card strong { display:block; overflow:hidden; color:#24364f; font-size:16px; font-weight:650; text-overflow:ellipsis; white-space:nowrap; }.metric-card small { display:block; overflow:hidden; margin-top:3px; color:#8594a7; font-size:9px; text-overflow:ellipsis; white-space:nowrap; }.metric-state { margin-left:auto; color:#74a491; font-size:8px; letter-spacing:.1em; }
.runtime-board { max-width:1180px; margin:0 auto 26px; padding:18px 20px 16px; border:1px solid #dce6ef; border-radius:13px; background:#fff; box-shadow:0 3px 13px #3c5c7c0c; }.runtime-board-heading { display:flex; align-items:flex-end; justify-content:space-between; margin-bottom:13px; }.runtime-board-heading h2 { margin:7px 0 0; color:#263851; font-size:18px; font-weight:650; }.runtime-board-heading>span { color:#7b93aa; font-size:9px; letter-spacing:.12em; }.runtime-board-grid { display:grid; grid-template-columns:repeat(3, 1fr); gap:8px; }.runtime-board-item { display:flex; align-items:center; min-width:0; min-height:46px; padding:8px 10px; border:1px solid #e4ebf2; border-radius:8px; background:#fbfcfe; }.runtime-board-item>i { flex:0 0 auto; width:7px; height:7px; margin-right:9px; border-radius:50%; }.runtime-dot-live { background:#50b993; box-shadow:0 0 0 4px #50b99320; }.runtime-dot-compat { background:#5f91de; box-shadow:0 0 0 4px #5f91de20; }.runtime-dot-pending { background:#d3a64c; box-shadow:0 0 0 4px #d3a64c20; }.runtime-dot-fallback { background:#c78a72; box-shadow:0 0 0 4px #c78a7220; }.runtime-board-item div { min-width:0; flex:1; }.runtime-board-item strong,.runtime-board-item small { display:block; overflow:hidden; text-overflow:ellipsis; white-space:nowrap; }.runtime-board-item strong { color:#334961; font-size:10px; font-weight:600; }.runtime-board-item small { margin-top:4px; color:#8a9aab; font-size:8px; }.runtime-board-item>b { margin-left:7px; font-size:8px; letter-spacing:.08em; }.runtime-state-live { color:#3a9a7d; }.runtime-state-compat { color:#4b7fca; }.runtime-state-pending { color:#af7a24; }.runtime-state-fallback { color:#b26b4d; }
.showcase-workbench { max-width:1180px; margin:0 auto 26px; padding:30px; border:1px solid #dce6ef; border-radius:14px; background:#fff; }.workbench-heading { justify-content:space-between; margin-bottom:20px; }.workbench-heading h2 { margin:8px 0 0; color:#263851; font-size:22px; font-weight:650; }.workbench-caption { color:#91a0b2; font-size:9px; letter-spacing:.11em; }.module-layout { display:flex; }.module-list { flex:0 0 35%; padding-right:20px; }.module-item { width:100%; margin-bottom:9px; padding:13px 14px; border:1px solid #e0e8f1; border-radius:9px; color:#48607b; background:#fbfcfe; cursor:pointer; text-align:left; transition:transform 160ms ease, border-color 160ms ease, box-shadow 160ms ease; }.module-item:hover,.module-item.selected { border-color:#99bdf0; box-shadow:0 7px 18px #37689d12; transform:translateX(3px); }.module-icon { display:inline-flex; align-items:center; justify-content:center; width:29px; height:29px; margin-right:10px; border-radius:8px; font-size:17px; }.module-mint .module-icon { color:#2d9b80; background:#ddf8ef; }.module-blue .module-icon { color:#467ad7; background:#e3edff; }.module-amber .module-icon { color:#ac7e2c; background:#fff2d8; }.module-item small,.module-item strong { display:block; }.module-item small { margin-bottom:2px; color:#94a3b5; font-size:8px; letter-spacing:.12em; }.module-item strong { color:#324962; font-size:12px; font-weight:600; }.module-item>b { margin-left:auto; color:#a1b0c0; font-size:15px; font-weight:400; }.module-detail { position:relative; flex:1; min-height:230px; padding:23px 25px; overflow:hidden; border-radius:11px; color:#edf7ff; background:#22394f; }.module-detail::after { content:""; position:absolute; right:-35px; bottom:-60px; width:210px; height:210px; border:1px solid #72d9bf33; border-radius:50%; animation:showcase-ring 5s ease-in-out infinite; }.detail-blue { background:#253c60; }.detail-amber { background:#4c3d28; }.detail-head { justify-content:space-between; }.detail-badge { color:#79e1c6; font-size:9px; font-weight:700; letter-spacing:.14em; }.detail-live { color:#9fb6cc; font-size:9px; }.detail-live i { display:inline-block; width:5px; height:5px; margin-right:5px; border-radius:50%; background:#72d9bf; box-shadow:0 0 0 4px #72d9bf22; }.module-detail h3 { margin:23px 0 7px; font-size:23px; font-weight:600; }.module-detail>p { max-width:360px; margin:0; color:#b6cade; font-size:12px; line-height:1.7; }.automation-preview { position:relative; z-index:1; max-width:360px; margin-top:20px; }.automation-line { height:27px; color:#bbd0e0; font-size:10px; }.automation-line b { margin-left:auto; color:#79dfc1; font-size:9px; font-weight:400; }.line-dot { width:7px; height:7px; margin-right:10px; border:1px solid #67809b; border-radius:50%; }.line-dot.done { border-color:#72d9bf; background:#72d9bf; box-shadow:0 0 0 4px #72d9bf22; }.line-dot.active { border-color:#f0bf67; background:#f0bf67; box-shadow:0 0 0 4px #f0bf6722; animation:showcase-blink 1.5s ease-in-out infinite; }.insight-preview { position:relative; z-index:1; display:flex; align-items:center; margin-top:18px; max-width:400px; color:#79e1c6; }.insight-number { flex:0 0 98px; }.insight-number strong { display:block; font-size:32px; font-weight:600; }.insight-number span { color:#9fb6cc; font-size:9px; }.insight-preview svg { width:230px; height:64px; color:#79e1c6; opacity:.8; }.security-preview { position:relative; z-index:1; margin-top:22px; }.security-shield { display:inline-flex; align-items:center; justify-content:center; width:48px; height:48px; margin-right:13px; border:1px solid #f0bf67; border-radius:15px; color:#f0bf67; font-size:25px; }.security-preview strong,.security-preview small { display:block; }.security-preview strong { color:#fff1cf; font-size:12px; }.security-preview small { margin-top:4px; color:#cbb998; font-size:9px; }.detail-footer { position:absolute; z-index:1; left:25px; right:25px; bottom:18px; justify-content:space-between; color:#91aac0; font-size:9px; }.detail-footer button { border:0; color:#79e1c6; background:transparent; cursor:pointer; font-size:10px; }.detail-footer button b { margin-left:6px; font-size:14px; font-weight:400; }
.detail-feedback { position:relative; z-index:1; min-height:19px; margin-top:13px; padding-right:20px; color:#91aac0; font-size:9px; line-height:1.45; }.detail-feedback-pending { color:#f0bf67; }.detail-feedback-success { color:#79e1c6; }.detail-feedback-error { color:#ffb09c; }
.showcase-bottom { max-width:1180px; margin:0 auto; padding:10px 2px; }.bottom-quote { flex:1; padding-right:40px; }.bottom-quote blockquote { max-width:520px; margin:10px 0; color:#2a3d57; font-size:18px; line-height:1.45; }.bottom-quote>span { color:#8a99aa; font-size:10px; }.bottom-actions { flex:0 0 47%; }.bottom-actions button { position:relative; flex:1; min-height:74px; margin-left:10px; padding:13px 37px 13px 15px; border:1px solid #dce6ef; border-radius:8px; color:#405974; background:#fff; cursor:pointer; text-align:left; transition:transform 160ms ease, box-shadow 160ms ease; }.bottom-actions button:hover { transform:translateY(-3px); box-shadow:0 10px 20px #47678518; }.bottom-actions span,.bottom-actions strong,.bottom-actions small { display:block; }.bottom-actions span { color:#7f9bb7; font-size:9px; }.bottom-actions strong { margin-top:5px; font-size:12px; }.bottom-actions small { margin-top:3px; color:#93a1b2; font-size:9px; }.bottom-actions b { position:absolute; top:24px; right:15px; color:#2f6fed; font-size:16px; font-weight:400; }
@keyframes showcase-grid { from { background-position:0 0; } to { background-position:0 28px; } } @keyframes showcase-ring { 0%,100% { opacity:.35; transform:translate(-50%,-50%) scale(.96); } 50% { opacity:.85; transform:translate(-50%,-50%) scale(1.04); } } @keyframes showcase-core-safe { 0%,100% { transform:translateY(0) scale(1); } 50% { transform:translateY(-7px) scale(1.05); } } @keyframes showcase-shine-safe { 0%,100% { opacity:.05; } 50% { opacity:.22; } } @keyframes showcase-orbit-safe { 0%,100% { opacity:.45; transform:translate(-50%,-50%) scale(.96); } 50% { opacity:.9; transform:translate(-50%,-50%) scale(1.04); } } @keyframes showcase-blink { 0%,100% { opacity:.4; } 50% { opacity:1; } }
.showcase-topbar { animation:showcase-enter 420ms cubic-bezier(.2,.7,.2,1) both; }.showcase-hero { animation:showcase-enter 520ms 70ms cubic-bezier(.2,.7,.2,1) both; }.showcase-metrics .metric-card { animation:showcase-enter 420ms cubic-bezier(.2,.7,.2,1) both; }.showcase-metrics .metric-card:nth-child(2) { animation-delay:70ms; }.showcase-metrics .metric-card:nth-child(3) { animation-delay:140ms; }.showcase-metrics .metric-card:nth-child(4) { animation-delay:210ms; }.showcase-workbench { animation:showcase-enter 480ms 190ms cubic-bezier(.2,.7,.2,1) both; }.showcase-bottom { animation:showcase-enter 420ms 260ms cubic-bezier(.2,.7,.2,1) both; } @keyframes showcase-enter { from { opacity:0; transform:translateY(10px); } to { opacity:1; transform:translateY(0); } }
.showcase-cpu .visual-noise { animation:none; opacity:.08; background-image:none; }.showcase-cpu .visual-core { box-shadow:0 0 0 10px #79e1c622,0 0 20px #2f6fed44; }.showcase-cpu .visual-orbit { animation:none; opacity:.4; }.showcase-cpu .module-detail::after { animation:none; opacity:.25; }.showcase-cpu .module-item:hover { transform:none; }.showcase-cpu .showcase-hero::after { animation:none; }
@media (prefers-reduced-motion: reduce) { .showcase-topbar,.showcase-hero,.showcase-metrics .metric-card,.showcase-workbench,.showcase-bottom { animation:none; } }
@media (max-width:980px) { .product-showcase { padding:24px; }.showcase-hero { padding:34px; }.hero-copy { width:62%; }.hero-visual { right:-4%; width:43%; opacity:.8; }.showcase-metrics { flex-wrap:wrap; }.metric-card { flex-basis:calc(50% - 8px); margin-bottom:10px; }.metric-card:nth-child(2) { margin-right:0; }.module-layout { flex-direction:column; }.module-list { display:flex; flex:initial; padding:0 0 15px; }.module-item { margin:0 8px 0 0; }.module-item:last-child { margin-right:0; }.module-item>b { display:none; }.module-item strong { font-size:11px; }.module-detail { min-height:260px; } }
@media (max-width:700px) { .product-showcase { padding:18px 14px 30px; }.showcase-topbar { margin-bottom:17px; }.showcase-runtime { display:none; }.showcase-hero { min-height:480px; padding:27px 23px; }.hero-copy { width:100%; }.hero-copy h1 { font-size:31px; }.hero-visual { top:auto; right:0; bottom:2px; width:100%; height:220px; opacity:.8; }.hero-actions { flex-wrap:wrap; }.showcase-link-button { margin-left:0; margin-top:12px; flex-basis:100%; text-align:left; }.showcase-tour { margin-left:0; margin-top:10px; }.hero-proof { display:none; }.metric-card { flex-basis:100%; margin-right:0; }.module-list { overflow:auto; }.module-item { min-width:145px; }.showcase-workbench { padding:20px 15px; }.workbench-heading { display:block; }.workbench-caption { display:block; margin-top:10px; }.showcase-bottom { display:block; }.bottom-quote { padding:0; }.bottom-actions { margin-top:17px; }.bottom-actions button { margin:0 8px 0 0; }.bottom-actions button:last-child { margin-right:0; } }
@media (max-width:980px) { .runtime-board-grid { grid-template-columns:repeat(2, 1fr); } }
@media (max-width:700px) { .runtime-board { padding:15px 13px; }.runtime-board-heading { display:block; }.runtime-board-heading>span { display:block; margin-top:8px; }.runtime-board-grid { grid-template-columns:1fr; } }
</style>
