<script setup lang="ts">
import { ref, onMounted, onBeforeUnmount, watch } from 'vue';
import type { SouiNativeClientImpl } from '../../../packages/sdk/soui_native_client';
import { FrameIntervalWindow } from './frame-interval-window';
const props = defineProps<{ nativeClient?: SouiNativeClientImpl | null }>();
const enabled = ref(true);
const detailed = ref(false);
const running = ref(false);
const fps = ref(0), p95 = ref(0), slow = ref(0);
const rendererBackend = ref('software-skia-gdi');
const motionQuality = ref<'high' | 'low'>('high');
const timingIssue = ref('等待样本');
const duplicateTimestamps = ref(0);
const history = ref<number[]>([]);
const marker = ref<HTMLElement | null>(null);
const paints = ref<string>('—');
const presented = ref<string>('—');
const diagnosticReadback = ref(false);
const displayHz = ref(0);
const drawIntervalMs = ref(17);
const targetHz = ref(60), activeHz = ref(60), settingRate = ref(false), rateError = ref('');
async function setTarget(rate: number) {
  if (!props.nativeClient || settingRate.value) return;
  settingRate.value = true; rateError.value = '';
  try { const result = await props.nativeClient.call('desktop.rendering.setTargetRate', {targetHz:rate}); if (!disposed) targetHz.value = result.requestedHz; }
  catch (error) { if (!disposed) rateError.value = String(error); }
  finally { settingRate.value = false; }
}
let lastNative: {paintCount:number;presentCount:number;sampledAtMs:number} | undefined;
let requesting = false, disposed = false, generation = 0;
let qualityBadSamples = 0, qualityGoodSamples = 0;
function publishMotionQuality(quality: 'low' | 'high') {
  motionQuality.value = quality;
  const event = document.createEvent('CustomEvent');
  event.initCustomEvent('soui-motion-quality', false, false, { quality });
  window.dispatchEvent(event);
}
async function readNative() {
  if (!props.nativeClient || requesting) return;
  requesting = true;
  const requestGeneration = generation;
  try {
    const result = await props.nativeClient.call('desktop.rendering.getStats', {});
    if (disposed || !enabled.value || requestGeneration !== generation) return;
    targetHz.value = result.requestedHz; activeHz.value = result.activePacingHz;
    displayHz.value = Number(result.displayRefreshHz ?? 0);
    drawIntervalMs.value = Number(result.drawIntervalMs ?? 0);
    rendererBackend.value = result.rendererBackend || result.gpuStatus || 'software-skia-gdi';
    (window as Window & { __souiRendererBackend?: string }).__souiRendererBackend = rendererBackend.value;
    diagnosticReadback.value = result.diagnosticReadback === true;
    const presentCount = Number(result.presentCount ?? 0);
    if (lastNative && result.sampledAtMs > lastNative.sampledAtMs) {
      const elapsed = result.sampledAtMs-lastNative.sampledAtMs;
      paints.value = (Math.max(0, result.paintCount-lastNative.paintCount)*1000/elapsed).toFixed(1);
      presented.value = presentCount > 0 && presentCount >= lastNative.presentCount
        ? ((presentCount-lastNative.presentCount)*1000/elapsed).toFixed(1) : '—';
    }
    lastNative = { paintCount: Number(result.paintCount ?? 0), presentCount, sampledAtMs: result.sampledAtMs };
  } catch { if (!disposed) paints.value = '不可用'; } finally { requesting = false; }
}
const intervals = new FrameIntervalWindow();
let frame = 0, previous = 0, reportAt = 0, distinctFrames = 0, duplicateFrames = 0;
function stop() { generation++; cancelAnimationFrame(frame); frame = 0; previous = 0; reportAt = 0; intervals.reset(); distinctFrames = 0; duplicateFrames = 0; lastNative = undefined; timingIssue.value = '等待样本'; fps.value = 0; paints.value = '—'; presented.value = '—'; duplicateTimestamps.value = 0; }
function tick(now: number) {
  const wallNow = performance.now();
  if (previous) {
    if (now > previous) { distinctFrames++; intervals.record(now - previous); }
    else duplicateFrames++;
  }
  previous = now;
  if (!reportAt) reportAt = wallNow;
  if (running.value && marker.value) marker.value.style.transform = `translateX(${(Math.sin(now / 500) + 1) * 90}px)`;
  if (wallNow - reportAt >= 1000) {
    // Never extrapolate one second from a truncated tail of intervals.
    fps.value = Math.round(distinctFrames * 10000 / (wallNow - reportAt)) / 10;
    duplicateTimestamps.value = duplicateFrames;
    const summary = intervals.summarize(targetHz.value);
    if (summary.state === 'ready') {
      timingIssue.value = '';
      p95.value = Math.round(summary.p95 * 10) / 10;
      slow.value = Math.round(summary.overBudgetPercent);
      const softwarePath = rendererBackend.value.indexOf('software') === 0;
      const budget = 1000 / Math.max(1, targetHz.value);
      if (softwarePath && summary.p95 > budget * 1.35) {
        qualityBadSamples++; qualityGoodSamples = 0;
        if (qualityBadSamples >= 2) publishMotionQuality('low');
      } else if (!softwarePath || summary.p95 <= budget * 1.1) {
        qualityGoodSamples++; qualityBadSamples = 0;
        if (qualityGoodSamples >= 4) publishMotionQuality('high');
      }
    } else {
      timingIssue.value = summary.state === 'overflow' ? '样本超限' : summary.state === 'invalid' ? '样本无效' : '样本不足';
    }
    history.value = history.value.concat(fps.value).slice(-40);
    intervals.reset(); distinctFrames = 0; duplicateFrames = 0; reportAt = wallNow;
    void readNative();
  }
  frame = requestAnimationFrame(tick);
}
function restart() { stop(); if (enabled.value && !document.hidden) frame = requestAnimationFrame(tick); }
watch(enabled, restart);
watch(detailed, value => { if (!value) running.value = false; });
onMounted(() => { document.addEventListener('visibilitychange', restart); restart(); void readNative(); });
onBeforeUnmount(() => { disposed = true; stop(); document.removeEventListener('visibilitychange', restart); });
</script>
<template>
  <aside class="frame-meter" aria-label="实时页面帧率">
    <button class="meter-toggle" :aria-expanded="enabled" aria-controls="frame-meter-body" @click="enabled = !enabled">{{ enabled ? '收起 FPS' : '显示 FPS' }}</button>
    <div v-if="enabled" id="frame-meter-body" class="meter-body">
      <div class="meter-reading"><strong>{{ fps || '—' }}</strong><span>rAF 节拍 / 秒</span></div>
      <p v-if="duplicateTimestamps">重复时间戳 {{duplicateTimestamps}}</p>
      <p v-if="fps && targetHz && fps > targetHz * 2">rAF 未绑定屏幕 vsync · 以 p95 与调度频率为准</p>
      <p v-if="timingIssue">{{timingIssue}} · p95 不可用</p>
      <p v-else>p95 {{ p95 }} ms · <span v-if="activeHz === targetHz">超预算 {{ slow }}%</span><span v-else>目标档未激活</span></p>
      <p v-if="presented !== '—'">实际呈现 {{ presented }} 帧/秒 · 原生绘制 {{ paints }} 次/秒</p>
      <p v-else-if="rendererBackend.indexOf('gpu-direct') === 0 && !diagnosticReadback">GPU 直通呈现 · 等待呈现样本</p>
      <p v-else>原生绘制 {{ paints }} 次/秒</p>
      <p>渲染后端 {{ rendererBackend }}</p>
      <p v-if="motionQuality === 'low'">CPU 动效降级：保留核心反馈，暂停装饰层动画</p>
      <p v-else-if="rendererBackend.indexOf('software') === 0">CPU 动效层级：正常</p>
      <p>目标 {{targetHz}}Hz · 调度 {{activeHz}}Hz</p>
      <p>显示器 {{displayHz || '未知'}}Hz</p>
      <p>合成预算 {{ drawIntervalMs ? `${drawIntervalMs} ms` : 'GPU 直通' }}</p>
      <small>rAF 是回调统计；“实际呈现”来自 native present/readback 计数。</small>
      <button class="meter-details-toggle" :aria-expanded="detailed" aria-controls="frame-meter-details" @click="detailed = !detailed">{{ detailed ? '收起详情' : '详情与帧率设置' }}</button>
      <div v-if="detailed" id="frame-meter-details">
      <p>目标帧预算 {{ (1000 / targetHz).toFixed(2) }} ms</p>
      <div class="rate-controls"><button v-for="rate in [60,120,144]" :key="rate" :aria-pressed="targetHz===rate" :disabled="settingRate || !nativeClient" @click="setTarget(rate)">{{rate}}</button></div>
      <small v-if="targetHz>60">实验调度；非实际帧率保证。切后台恢复常规调度，可能增加功耗。</small>
      <p v-if="rateError" role="alert">{{rateError}}</p>
      <svg width="100%" height="32" viewBox="0 0 200 32" aria-hidden="true"><polyline :points="history.map((value, index) => `${index * 5},${32 - Math.min(180, value) / 180 * 32}`).join(' ')" fill="none" stroke="#69d3b1" stroke-width="2" /></svg>
      <button @click="running = !running">{{ running ? '停止运动样例' : '运行运动样例' }}</button>
      <div v-show="running" class="meter-track"><i ref="marker"></i></div>
      <small>rAF / 绘制回调均非屏幕呈现帧率。<br>无 GPU 时自动回退软件 Skia/GDI。</small>
      </div>
    </div>
  </aside>
</template>
<style>
.frame-meter { position: fixed; left: 12px; bottom: 38px; z-index: 3000; width: 208px; max-height: calc(100vh - 318px); overflow-y: auto; border-radius: 8px; color: #e3eefc; background: #202d40; box-shadow: 0 4px 16px #0002; font-size: 11px; }
.frame-meter .meter-details-toggle { margin-top:8px; width:100%; text-align:left; }
@media(max-width:1100px) { .frame-meter { width:176px; } }
@media(max-width:720px) { .frame-meter { left:auto; right:12px; bottom:12px; width:208px; max-height:45vh; } }
.frame-meter button { border: 1px solid #5c6e85; border-radius: 4px; color: #e3eefc; background: #2d3f57; font-size: 11px; cursor: pointer; padding: 5px 8px; }
.frame-meter .meter-toggle { width: 100%; text-align: left; border: 0; padding: 9px 12px; }
.rate-controls { display:flex; margin:8px 0; }.rate-controls button { margin-right:8px; }.rate-controls button[aria-pressed=true] { border-color:#69d3b1; color:#69d3b1; }
.meter-body { padding: 2px 12px 12px; }.meter-reading { display: flex; align-items: baseline; }.meter-reading strong { font-size: 27px; margin-right: 8px; color: #69d3b1; }.meter-body p { margin: 4px 0; }.meter-body small { display: block; margin-top: 8px; line-height: 1.6; color: #b1c0d5; }.meter-track { position: relative; height: 16px; margin-top: 9px; background: #142133; }.meter-track i { position: absolute; top: 3px; left: 0; width: 10px; height: 10px; background: #69d3b1; border-radius: 3px; }
</style>
