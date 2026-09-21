<script setup lang="ts">
import { ref, defineAsyncComponent, onMounted, onBeforeUnmount } from 'vue';
import { KitButton, KitSwitch, KitSlider } from './kit-ui';
import type { SouiNativeClientImpl } from '../../../packages/sdk/soui_native_client';
const NativeActions = defineAsyncComponent(() => import('./NativeActions.vue'));

const props = defineProps<{ nativeConnected: boolean; nativeClient?: SouiNativeClientImpl | null; rendererBackend?: string }>();
const emit = defineEmits<{ (event: 'scan'): void }>();
const motion = ref(!(window.matchMedia && window.matchMedia('(prefers-reduced-motion: reduce)').matches));
const duration = ref(280);
const expanded = ref(false);
const selected = ref(0);
const replay = ref(0);
const burst = ref(false);
const parallaxX = ref(0);
const parallaxY = ref(0);
const motionQualityLow = ref(false);
const rendererMode = ref(props.rendererBackend || 'software-skia-gdi');
let rendererModeTimer: number | undefined;
function onMotionQuality(event: Event) {
  const quality = (event as CustomEvent<{ quality?: string }>).detail?.quality;
  motionQualityLow.value = quality === 'low';
}
async function refreshRendererMode() {
  if (!props.nativeClient) return;
  const result = await props.nativeClient.call('desktop.rendering.getStats', {}).catch(() => undefined);
  if (result) rendererMode.value = result.rendererBackend || result.gpuStatus || rendererMode.value;
}
onMounted(() => {
  window.addEventListener('soui-motion-quality', onMotionQuality);
  void refreshRendererMode();
  rendererModeTimer = window.setInterval(() => { void refreshRendererMode(); }, 1000);
});
onBeforeUnmount(() => {
  window.removeEventListener('soui-motion-quality', onMotionQuality);
  if (rendererModeTimer !== undefined) window.clearInterval(rendererModeTimer);
  rendererModeTimer = undefined;
});
function updateParallax(event: MouseEvent) {
  const target = event.currentTarget as HTMLElement;
  const rect = target.getBoundingClientRect();
  parallaxX.value = ((event.clientX - rect.left) / rect.width - 0.5) * 14;
  parallaxY.value = ((event.clientY - rect.top) / rect.height - 0.5) * 10;
}
function resetParallax() {
  parallaxX.value = 0;
  parallaxY.value = 0;
}
const modes = ['浏览', '整理', '检查'];
</script>

<template>
  <section class="studio" :class="{ 'motion-off': !motion, 'motion-cpu-safe': rendererMode.indexOf('gpu') !== 0, 'motion-quality-low': motionQualityLow }" aria-labelledby="studio-title">
    <header class="studio-heading">
      <div><p class="eyebrow">INTERACTION STUDIO</p><h1 id="studio-title">交互，从这里开始。</h1><p class="lede">成熟组件 · 本地运行 · 原生能力</p></div>
      <span class="studio-version">ELEMENT PLUS / 2.11.1</span>
    </header>
    <div class="studio-stage">
      <div class="stage-caption"><span>桌面交互预览</span><span>01 / WORKSPACE</span></div>
      <div :key="replay" class="preview-window" :style="{ animationDuration: `${motion ? duration : 0}ms` }">
        <div class="preview-toolbar"><span class="window-emblem" aria-hidden="true"></span><strong>文件工作空间</strong><span class="local-label">离线可用</span></div>
        <div class="preview-body">
          <div class="preview-overview"><span class="folder-art" aria-hidden="true"></span><div><p class="eyebrow">YOUR NEXT WORKSPACE</p><h2>让界面响应每一次操作</h2><p>切换视图、打开详情，再进入真实目录扫描。</p></div></div>
          <div class="mode-strip" role="group" aria-label="预览视图">
            <span class="mode-indicator" :style="{ transform: `translateX(${selected * 100}%)`, transitionDuration: `${motion ? duration : 0}ms` }" aria-hidden="true"></span>
            <button v-for="(mode, index) in modes" :key="mode" type="button" :aria-pressed="selected === index" @click="selected = index">{{ mode }}</button>
          </div>
          <div class="preview-content" aria-live="polite">
            <!-- Commit the new state immediately; do not wait for an exit animation. -->
            <div :key="selected" class="preview-message"><h3>{{ ['专注内容，不被操作打断', '清晰的结构，有序的反馈', '看得见状态，找得到原因'][selected] }}</h3><p>{{ ['列表保留滚动位置，详情随时展开。', '当前是交互预览，不移动或修改你的文件。', '原生调用状态与界面预览分别标注，不伪造任务结果。'][selected] }}</p></div>
          </div>
          <div class="preview-actions"><KitButton type="primary" @click="emit('scan')">打开原生扫描</KitButton><KitButton :aria-expanded="expanded" aria-controls="studio-inspector" @click="expanded = !expanded">{{ expanded ? '收起详情' : '展开详情' }}</KitButton></div>
          <Transition name="studio-detail"><div v-if="expanded" id="studio-inspector" class="preview-inspector" :style="{ transitionDuration: `${motion ? duration : 0}ms` }"><strong>交互说明</strong><p>按钮、开关、滑块来自 Element Plus；过渡由统一动效参数控制。</p><span>{{ nativeConnected ? '标准 SDK 已连接；扫描页调用真实 C++。' : '当前为 Web 预览，原生能力未连接。' }}</span><NativeActions :client="nativeClient" /></div></Transition>
        </div>
      </div>
      <div class="stage-footnote"><span class="stage-line"></span>可操作的组件预览，不是视频或静态效果图。</div>
    </div>
    <section class="motion-controls" aria-label="动效设置">
      <div class="motion-control"><label for="studio-motion">界面动效</label><p>随时关闭，内容与功能保持完整</p><KitSwitch id="studio-motion" v-model="motion" aria-label="界面动效" /></div>
      <div class="motion-control duration-control"><label id="duration-label">过渡时长 <strong>{{ duration }} ms</strong></label><KitSlider v-model="duration" :min="120" :max="480" :step="20" :show-tooltip="false" :disabled="!motion" aria-labelledby="duration-label" /></div>
      <KitButton @click="replay++">回放入场</KitButton>
    </section>
    <section class="motion-showcase" aria-labelledby="motion-showcase-title">
      <div class="motion-showcase-copy">
        <p class="eyebrow">COMPOSITOR MOTION LAB</p>
        <h2 id="motion-showcase-title">把每一次变化，做成有重量的反馈。</h2>
        <p>动画只改变 transform 与 opacity；GPU 可用时走 direct present，没有 GPU 时仍由软件 Skia 平滑完成。</p>
        <div class="motion-tags"><span>transform</span><span>opacity</span><span>reduced-motion</span></div>
        <KitButton class="motion-trigger" size="small" @click="burst = !burst">{{ burst ? '停止能量脉冲' : '触发能量脉冲' }}</KitButton>
      </div>
      <div class="motion-stage" :class="{ 'motion-burst': burst }" aria-label="动效演示" @mousemove="updateParallax" @mouseleave="resetParallax">
        <div class="motion-grid" aria-hidden="true"></div>
        <div class="motion-parallax" :style="{ transform: `translate3d(${parallaxX}px, ${parallaxY}px, 0)` }">
          <div class="motion-echo motion-echo-a" aria-hidden="true"></div>
          <div class="motion-echo motion-echo-b" aria-hidden="true"></div>
          <div class="motion-scanline" aria-hidden="true"></div>
          <div class="motion-orbit motion-orbit-a" aria-hidden="true"><i></i></div>
          <div class="motion-orbit motion-orbit-b" aria-hidden="true"><i></i></div>
          <div class="motion-orbit motion-orbit-c" aria-hidden="true"><i></i></div>
          <div class="motion-core" aria-hidden="true"><span></span></div>
          <div class="motion-trail" aria-hidden="true"><i></i><i></i><i></i></div>
          <div class="motion-pulse-bars" aria-hidden="true"><i></i><i></i><i></i><i></i><i></i></div>
        </div>
        <span class="motion-status">{{ rendererMode.indexOf('gpu') === 0 ? (rendererMode.indexOf('gpu-direct') === 0 ? 'GPU DIRECT / TRANSFORM SAFE' : 'GPU READBACK / TRANSFORM SAFE') : 'CPU SAFE / TRANSFORM ONLY' }}</span>
      </div>
    </section>
    <p class="rendering-note">渲染说明：GPU direct 为实验后端；没有可用 GPU 时自动使用软件 Skia/GDI，界面动效保持可用。</p>
  </section>
</template>
